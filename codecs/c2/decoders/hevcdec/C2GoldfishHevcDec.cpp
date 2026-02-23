/*
 * Copyright 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string_view>

#include <inttypes.h>
#include <log/log.h>
#include <media/stagefright/foundation/AUtils.h>
#include <media/stagefright/foundation/MediaDefs.h>

#include <C2AllocatorGralloc.h>
#include <C2PlatformSupport.h>

#include <android/hardware/graphics/allocator/3.0/IAllocator.h>
#include <android/hardware/graphics/mapper/3.0/IMapper.h>
#include <hidl/LegacySupport.h>

#include <media/stagefright/foundation/MediaDefs.h>

#include <C2Debug.h>
#include <C2PlatformSupport.h>
#include <Codec2Mapper.h>
#include <SimpleC2Interface.h>
#include <gralloc_cb_bp.h>

#include <color_buffer_utils.h>

#include "C2GoldfishHevcDecFactory.h"

#include <atomic>
#include <map>
#include <mutex>
#include <sys/time.h>
#include <SimpleC2Component.h>
#include <media/stagefright/foundation/ColorUtils.h>
#include "GoldfishHevcHelper.h"
#include "MediaHevcDecoder.h"

#define DEBUG 0
#if DEBUG
#define DDD(...) ALOGD(__VA_ARGS__)
#else
#define DDD(...) ((void)0)
#endif

#define ALIGN2(x) ((((x) + 1) >> 1) << 1)

namespace goldfish::media::c2 {

using ::android::hardware::graphics::common::V1_0::BufferUsage;
using ::android::hardware::graphics::common::V1_2::PixelFormat;

using ::android::C2Mapper;
using ::android::ColorAspects;
using ::android::ColorUtils;
using ::android::UnwrapNativeCodec2GrallocHandle;
using ::android::status_t;

namespace {
constexpr size_t kMinInputBufferSize = 6 * 1024 * 1024;
constexpr char COMPONENT_NAME[] = "c2.goldfish.hevc.decoder";

void fillEmptyWork(const std::unique_ptr<C2Work> &work) {
    uint32_t flags = 0;
    if (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) {
        flags |= C2FrameData::FLAG_END_OF_STREAM;
        DDD("signalling eos");
    }
    DDD("fill empty work");
    work->worklets.front()->output.flags = (C2FrameData::flags_t)flags;
    work->worklets.front()->output.buffers.clear();
    work->worklets.front()->output.ordinal = work->input.ordinal;
    work->workletsProcessed = 1u;
}

struct C2GoldfishHevcDec : public SimpleC2Component {
    C2GoldfishHevcDec(const char *name, c2_node_id_t id,
                     const std::shared_ptr<C2BaseParams> &params)
            : SimpleC2Component(std::make_shared<SimpleC2Interface<C2BaseParams>>(name, id, params))
            , mParams(params)
            , mWidth(mParams->width())
            , mHeight(mParams->height())
    {
        DDD("creating hevc decoder now w %d h %d", mWidth, mHeight);
    }

    virtual ~C2GoldfishHevcDec() { onRelease(); }

    c2_status_t onInit() override {
        status_t err = initDecoder();
        return err == ::android::OK ? C2_OK : C2_CORRUPTED;
    }

    c2_status_t onStop() override {
        if (::android::OK != resetDecoder())
            return C2_CORRUPTED;
        resetPlugin();
        return C2_OK;
    }

    void onReset() override {
        (void)onStop();
    }

    void onRelease() override {
        deleteContext();
        if (mOutBlock) {
            mOutBlock.reset();
        }
    }

    c2_status_t onFlush_sm() override {
        if (::android::OK != setFlushMode())
            return C2_CORRUPTED;

        if (!mContext) {
            // just ignore if context is not even created
            return C2_OK;
        }

        while (true) {
            mPts = 0;
            constexpr bool hasPicture = false;
            setDecodeArgs(nullptr, 0, 0, 0, hasPicture);
            mImg = mContext->getImage();
            if (mImg.data == nullptr) {
                resetPlugin();
                break;
            }
        }

        deleteContext();
        return C2_OK;
    }

    void process(const std::unique_ptr<C2Work> &work,
                 const std::shared_ptr<C2BlockPool> &pool) override {
        // Initialize output work
        work->result = C2_OK;
        work->workletsProcessed = 0u;
        work->worklets.front()->output.flags = work->input.flags;
        if (mSignalledError || mSignalledOutputEos) {
            work->result = C2_BAD_VALUE;
            return;
        }

        DDD("process work");
        if (!mContext) {
            DDD("creating decoder context to host in process work");
            checkMode(pool);
            createDecoder();
            decodeHeaderAfterFlush();
        }

        size_t inSize = 0u;
        uint32_t workIndex = work->input.ordinal.frameIndex.peeku() & 0xFFFFFFFF;
        mPts = work->input.ordinal.timestamp.peeku();
        C2ReadView rView = mDummyReadView;
        if (!work->input.buffers.empty()) {
            rView =
                work->input.buffers[0]->data().linearBlocks().front().map().get();
            inSize = rView.capacity();
            if (inSize && rView.error()) {
                ALOGE("read view map failed %d", rView.error());
                work->result = rView.error();
                return;
            }
        }
        bool eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);
        bool hasPicture = (inSize > 0);

        DDD("in buffer attr. size %zu timestamp %d frameindex %d, flags %x", inSize,
            (int)work->input.ordinal.timestamp.peeku(),
            (int)work->input.ordinal.frameIndex.peeku(), work->input.flags);
        size_t inPos = 0;
        while (inPos < inSize) {
            if (C2_OK != ensureDecoderState(pool)) {
                mSignalledError = true;
                work->workletsProcessed = 1u;
                work->result = C2_CORRUPTED;
                return;
            }

            {
                if (work->input.flags & C2FrameData::FLAG_CODEC_CONFIG) {
                    hasPicture = false;
                }

                if (!setDecodeArgs(&rView, inPos, inSize - inPos, workIndex, hasPicture)) {
                    mSignalledError = true;
                    work->workletsProcessed = 1u;
                    work->result = C2_CORRUPTED;
                    return;
                }

                DDD("flag is %x", work->input.flags);
                if (work->input.flags & C2FrameData::FLAG_CODEC_CONFIG) {
                    if (mCsd0.empty()) {
                        mCsd0.assign(mInPBuffer, mInPBuffer + mInPBufferSize);
                        DDD("assign to csd0 with %d bytpes", mInPBufferSize);
                    }
                }

                bool whChanged = false;
                if (GoldfishHevcHelper::isKeyFrame(mInPBuffer, mInPBufferSize)) {
                    mHevcHelper = std::make_unique<GoldfishHevcHelper>(mWidth, mHeight);
                    bool headerStatus = true;
                    whChanged = mHevcHelper->decodeHeader(
                        mInPBuffer, mInPBufferSize, headerStatus);
                    if (!headerStatus) {
                        mSignalledError = true;
                        work->workletsProcessed = 1u;
                        work->result = C2_CORRUPTED;
                        return;
                    }
                    if (whChanged) {
                            DDD("w changed from old %d to new %d\n", mWidth, mHevcHelper->getWidth());
                            DDD("h changed from old %d to new %d\n", mHeight, mHevcHelper->getHeight());
                            if (1) {
                                drainInternal(DRAIN_COMPONENT_NO_EOS, pool, work);
                                resetDecoder();
                                resetPlugin();
                                work->workletsProcessed = 0u;
                            }
                            {
                                mWidth = mHevcHelper->getWidth();
                                mHeight = mHevcHelper->getHeight();
                                C2StreamPictureSizeInfo::output size(0u, mWidth, mHeight);
                                std::vector<std::unique_ptr<C2SettingResult>> failures;
                                c2_status_t err = mParams->config({&size}, C2_MAY_BLOCK, &failures);
                                if (err == ::android::OK) {
                                    work->worklets.front()->output.configUpdate.push_back(
                                            C2Param::Copy(size));
                                    ensureDecoderState(pool);
                                } else {
                                    ALOGE("Cannot set width and height");
                                    mSignalledError = true;
                                    work->workletsProcessed = 1u;
                                    work->result = C2_CORRUPTED;
                                    return;
                                }
                            }
                            if (!mContext) {
                                DDD("creating decoder context to host in process work");
                                checkMode(pool);
                                createDecoder();
                            }
                            continue;
                    } // end of whChanged
                } // end of isKeyFrame

                sendMetadata();

                DDD("decoding");
                GfResult hevcRes = mContext->decodeFrame(mInPBuffer, mInPBufferSize, mPts);
                mConsumedBytes = hevcRes.bytesProcessed;
                DDD("decoding consumed %d", (int)mConsumedBytes);

                if (mHostColorBufferId > 0) {
                    mImg = mContext->renderOnHostAndReturnImageMetadata(
                        mHostColorBufferId);
                } else {
                    mImg = mContext->getImage();
                }
            }
            if (mImg.data != nullptr) {
                DDD("got data %" PRIu64 " with pts %" PRIu64,  getWorkIndex(mImg.pts), mImg.pts);
                mHeaderDecoded = true;
                copyImageData(mImg);
                finishWork(getWorkIndex(mImg.pts), work);
                removePts(mImg.pts);
            } else {
                work->workletsProcessed = 0u;
            }

            inPos += mConsumedBytes;
        }
        if (eos) {
            DDD("drain because of eos");
            drainInternal(DRAIN_COMPONENT_WITH_EOS, pool, work);
            mSignalledOutputEos = true;
        } else if (!hasPicture) {
            DDD("no picture, fill empty work");
            fillEmptyWork(work);
        }

        work->input.buffers.clear();
    }

    c2_status_t drain(uint32_t drainMode,
                      const std::shared_ptr<C2BlockPool> &pool) override {
        DDD("drainInternal because of drain");
        return drainInternal(drainMode, pool, nullptr);
    }

private:
    void checkMode(const std::shared_ptr<C2BlockPool> &pool) {
        mWidth = mParams->width();
        mHeight = mParams->height();
        //const bool isGraphic = (pool->getLocalId() == C2PlatformAllocatorStore::GRALLOC);
        const bool isGraphic = (pool->getAllocatorId() & C2Allocator::GRAPHIC);
        DDD("buffer pool allocator id %x",  (int)(pool->getAllocatorId()));
        if (isGraphic) {
            uint64_t client_usage = getClientUsage(*pool);
            DDD("client has usage as 0x%llx", client_usage);
            if (client_usage & BufferUsage::CPU_READ_MASK) {
                DDD("decoding to guest byte buffer as client has read usage");
                mEnableAndroidNativeBuffers = false;
            } else {
                DDD("decoding to host color buffer");
                mEnableAndroidNativeBuffers = true;
            }
        } else {
            DDD("decoding to guest byte buffer");
            mEnableAndroidNativeBuffers = false;
        }
    }

    status_t createDecoder() {
        DDD("creating hevc context now w %d h %d", mWidth, mHeight);
        mContext = std::make_unique<MediaHevcDecoder>(
                mEnableAndroidNativeBuffers ?
                        RenderMode::RENDER_BY_HOST_GPU : RenderMode::RENDER_BY_GUEST_CPU);

        mContext->initHevcContext(mWidth, mHeight, mWidth, mHeight,
                                MediaHevcDecoder::PixelFormat::YUV420P);
        return ::android::OK;
    }

    status_t setParams(size_t stride) {
        (void)stride;
        return ::android::OK;
    }

    status_t initDecoder() {
        mStride = ALIGN2(mWidth);
        mSignalledError = false;
        resetPlugin();

        return ::android::OK;
    }

    bool setDecodeArgs(C2ReadView *inBuffer, size_t inOffset, size_t inSize,
                       uint32_t tsMarker, bool hasPicture) {
        if (inBuffer) {
            mInPBuffer = const_cast<uint8_t *>(inBuffer->data() + inOffset);
            mInPBufferSize = inSize;
            if (hasPicture) {
                insertPts(tsMarker, mPts);
            }
        }

        return true;
    }

    c2_status_t ensureDecoderState(const std::shared_ptr<C2BlockPool> &pool) {
        if (mOutBlock && (mOutBlock->width() != ALIGN2(mWidth) ||
                        mOutBlock->height() != mHeight)) {
            mOutBlock.reset();
        }
        if (!mOutBlock) {
            const uint32_t format = HAL_PIXEL_FORMAT_YCBCR_420_888;
            const C2MemoryUsage usage = {(uint64_t)(BufferUsage::VIDEO_DECODER),
                                        C2MemoryUsage::CPU_WRITE | C2MemoryUsage::CPU_READ};
            c2_status_t err = pool->fetchGraphicBlock(ALIGN2(mWidth), mHeight,
                                                    format, usage, &mOutBlock);
            if (err != C2_OK) {
                ALOGE("fetchGraphicBlock for Output failed with status %d", err);
                return err;
            }
            if (mEnableAndroidNativeBuffers) {
                auto c2Handle = mOutBlock->handle();
                native_handle_t *grallocHandle =
                    UnwrapNativeCodec2GrallocHandle(c2Handle);
                mHostColorBufferId = getColorBufferHandle(grallocHandle);
                DDD("found handle %d", mHostColorBufferId);
            }
            DDD("provided (%dx%d) required (%dx%d)", mOutBlock->width(),
                mOutBlock->height(), ALIGN2(mWidth), mHeight);
        }

        return C2_OK;
    }

    void finishWork(uint64_t index, const std::unique_ptr<C2Work> &work) {
        std::shared_ptr<C2Buffer> buffer =
            createGraphicBuffer(std::move(mOutBlock), C2Rect(mWidth, mHeight));
        mOutBlock = nullptr;
        {
            C2BaseParams::Lock lock = mParams->lock();
            buffer->setInfo(mParams->getColorAspects_l());
        }

        class FillWork {
        public:
            FillWork(uint32_t flags, C2WorkOrdinalStruct ordinal,
                    const std::shared_ptr<C2Buffer> &buffer)
                : mFlags(flags), mOrdinal(ordinal), mBuffer(buffer) {}
            ~FillWork() = default;

            void operator()(const std::unique_ptr<C2Work> &work) {
                work->worklets.front()->output.flags = (C2FrameData::flags_t)mFlags;
                work->worklets.front()->output.buffers.clear();
                work->worklets.front()->output.ordinal = mOrdinal;
                work->workletsProcessed = 1u;
                work->result = C2_OK;
                if (mBuffer) {
                    work->worklets.front()->output.buffers.push_back(mBuffer);
                }
                DDD("timestamp = %lld, index = %lld, w/%s buffer",
                    mOrdinal.timestamp.peekll(), mOrdinal.frameIndex.peekll(),
                    mBuffer ? "" : "o");
            }

        private:
            const uint32_t mFlags;
            const C2WorkOrdinalStruct mOrdinal;
            const std::shared_ptr<C2Buffer> mBuffer;
        };

        auto fillWork = [buffer](const std::unique_ptr<C2Work> &work) {
            work->worklets.front()->output.flags = (C2FrameData::flags_t)0;
            work->worklets.front()->output.buffers.clear();
            work->worklets.front()->output.buffers.push_back(buffer);
            work->worklets.front()->output.ordinal = work->input.ordinal;
            work->workletsProcessed = 1u;
        };
        if (work && c2_cntr64_t(index) == work->input.ordinal.frameIndex) {
            bool eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);
            if (eos) {
                if (buffer) {
                    C2WorkOrdinalStruct outOrdinal = work->input.ordinal;
                    DDD("%s %d: cloneAndSend ", __func__, __LINE__);
                    cloneAndSend(
                        index, work,
                        FillWork(C2FrameData::FLAG_INCOMPLETE, outOrdinal, buffer));
                    buffer.reset();
                }
            } else {
                DDD("%s %d: fill", __func__, __LINE__);
                fillWork(work);
            }
        } else {
            DDD("%s %d: finish", __func__, __LINE__);
            finish(index, fillWork);
        }
    }

    status_t setFlushMode() {
        if (mContext) {
            mContext->flush();
        }
        mHeaderDecoded = false;
        return ::android::OK;
    }

    c2_status_t drainInternal(uint32_t drainMode,
                              const std::shared_ptr<C2BlockPool> &pool,
                              const std::unique_ptr<C2Work> &work) {
        if (drainMode == NO_DRAIN) {
            ALOGW("drain with NO_DRAIN: no-op");
            return C2_OK;
        }
        if (drainMode == DRAIN_CHAIN) {
            ALOGW("DRAIN_CHAIN not supported");
            return C2_OMITTED;
        }

        if (::android::OK != setFlushMode())
            return C2_CORRUPTED;
        while (true) {
            if (C2_OK != ensureDecoderState(pool)) {
                mSignalledError = true;
                work->workletsProcessed = 1u;
                work->result = C2_CORRUPTED;
                return C2_CORRUPTED;
            }

            if (mHostColorBufferId > 0) {
                mImg = mContext->renderOnHostAndReturnImageMetadata(
                    mHostColorBufferId);
            } else {
                mImg = mContext->getImage();
            }

            if (mImg.data != nullptr) {
                DDD("got data in drain mode %" PRIu64 " with pts %" PRIu64,  getWorkIndex(mImg.pts), mImg.pts);
                copyImageData(mImg);
                finishWork(getWorkIndex(mImg.pts), work);
                removePts(mImg.pts);
            } else {
                fillEmptyWork(work);
                break;
            }
        }

        return C2_OK;
    }

    status_t resetDecoder() {
        mStride = 0;
        mSignalledError = false;
        mHeaderDecoded = false;
        deleteContext();
        return ::android::OK;
    }

    void resetPlugin() {
        mSignalledOutputEos = false;
        if (mOutBlock) {
            mOutBlock.reset();
        }
    }

    void deleteContext() {
        if (mContext) {
            mContext->destroyHevcContext();
            mContext.reset(nullptr);
            mPts2Index.clear();
            mOldPts2Index.clear();
            mIndex2Pts.clear();
        }
    }

    void removePts(uint64_t pts) {
        bool found = false;
        uint64_t index = 0;
        if (!mOldPts2Index.empty()) {
            auto iter = mOldPts2Index.find(pts);
            if (iter != mOldPts2Index.end()) {
                index = iter->second;
                mOldPts2Index.erase(iter);
                found = true;
            }
        } else {
            auto iter = mPts2Index.find(pts);
            if (iter != mPts2Index.end()) {
                index = iter->second;
                mPts2Index.erase(iter);
                found = true;
            }
        }

        if (!found) return;

        auto iter2 = mIndex2Pts.find(index);
        if (iter2 == mIndex2Pts.end()) return;
        mIndex2Pts.erase(iter2);
    }

    void insertPts(uint32_t work_index, uint64_t pts) {
        auto iter = mPts2Index.find(pts);
        if (iter != mPts2Index.end()) {
            DDD("inserted to old pts %" PRIu64 " with index %d", pts, (int)iter->second);
            mOldPts2Index[iter->first] = iter->second;
        }
        DDD("inserted pts %" PRIu64 " with index %d", pts, (int)work_index);
        mIndex2Pts[work_index] = pts;
        mPts2Index[pts] = work_index;
    }

    uint64_t getWorkIndex(uint64_t pts) {
        if (!mOldPts2Index.empty()) {
            auto iter = mOldPts2Index.find(pts);
            if (iter != mOldPts2Index.end()) {
                auto index = iter->second;
                DDD("found index %d for pts %" PRIu64, (int)index, pts);
                return index;
            }
        }
        auto iter = mPts2Index.find(pts);
        if (iter != mPts2Index.end()) {
            auto index = iter->second;
            DDD("found index %d for pts %" PRIu64, (int)index, pts);
            return index;
        }
        DDD("not found index for pts %" PRIu64, pts);
        return 0;
    }

    void getVuiParams(GfImage &img) {
        VuiColorAspects vuiColorAspects;
        vuiColorAspects.primaries = img.color_primaries;
        vuiColorAspects.transfer = img.color_trc;
        vuiColorAspects.coeffs = img.colorspace;
        vuiColorAspects.fullRange = img.color_range == 2 ? true : false;

        if (!(vuiColorAspects == mBitstreamColorAspects)) {
            mBitstreamColorAspects = vuiColorAspects;
            ColorAspects sfAspects;
            C2StreamColorAspectsInfo::input codedAspects = {0u};
            ColorUtils::convertIsoColorAspectsToCodecAspects(
                vuiColorAspects.primaries, vuiColorAspects.transfer,
                vuiColorAspects.coeffs, vuiColorAspects.fullRange, sfAspects);
            if (!C2Mapper::map(sfAspects.mPrimaries, &codedAspects.primaries)) {
                codedAspects.primaries = C2Color::PRIMARIES_UNSPECIFIED;
            }
            if (!C2Mapper::map(sfAspects.mRange, &codedAspects.range)) {
                codedAspects.range = C2Color::RANGE_UNSPECIFIED;
            }
            if (!C2Mapper::map(sfAspects.mMatrixCoeffs, &codedAspects.matrix)) {
                codedAspects.matrix = C2Color::MATRIX_UNSPECIFIED;
            }
            if (!C2Mapper::map(sfAspects.mTransfer, &codedAspects.transfer)) {
                codedAspects.transfer = C2Color::TRANSFER_UNSPECIFIED;
            }
            std::vector<std::unique_ptr<C2SettingResult>> failures;
            (void)mParams->config({&codedAspects}, C2_MAY_BLOCK, &failures);
        }
    }

    void copyImageData(GfImage &img) {
        getVuiParams(img);
        if (mEnableAndroidNativeBuffers)
            return;

        auto writeView = mOutBlock->map().get();
        if (writeView.error()) {
            ALOGE("graphic view map failed %d", writeView.error());
            return;
        }
        size_t dstYStride = writeView.layout().planes[C2PlanarLayout::PLANE_Y].rowInc;
        size_t dstUVStride = writeView.layout().planes[C2PlanarLayout::PLANE_U].rowInc;

        uint8_t *pYBuffer = const_cast<uint8_t *>(writeView.data()[C2PlanarLayout::PLANE_Y]);
        uint8_t *pUBuffer = const_cast<uint8_t *>(writeView.data()[C2PlanarLayout::PLANE_U]);
        uint8_t *pVBuffer = const_cast<uint8_t *>(writeView.data()[C2PlanarLayout::PLANE_V]);

        for (int i = 0; i < mHeight; ++i) {
            memcpy(pYBuffer + i * dstYStride, img.data + i * mWidth, mWidth);
        }
        for (int i = 0; i < mHeight / 2; ++i) {
            memcpy(pUBuffer + i * dstUVStride,
                img.data + mWidth * mHeight + i * mWidth / 2, mWidth / 2);
        }
        for (int i = 0; i < mHeight / 2; ++i) {
            memcpy(pVBuffer + i * dstUVStride,
                img.data + mWidth * mHeight * 5 / 4 + i * mWidth / 2,
                mWidth / 2);
        }
    }

    void sendMetadata() {
        MetaDataColorAspects currentMetaData = {1, 0, 0, 0};
        currentMetaData.primaries = mParams->primaries();
        currentMetaData.range = mParams->range();
        currentMetaData.transfer = mParams->transfer();

        DDD("metadata primaries %d range %d transfer %d",
                (int)(currentMetaData.primaries),
                (int)(currentMetaData.range),
                (int)(currentMetaData.transfer)
        );

        if (mSentMetadata.primaries == currentMetaData.primaries &&
            mSentMetadata.range == currentMetaData.range &&
            mSentMetadata.transfer == currentMetaData.transfer) {
            DDD("metadata is the same, no need to update");
            return;
        }
        std::swap(mSentMetadata, currentMetaData);

        mContext->sendMetadata(&(mSentMetadata));
    }

    void decodeHeaderAfterFlush() {
        DDD("calling %s", __func__);
        if (mContext && !mCsd0.empty()) {
            mContext->decodeFrame(&(mCsd0[0]), mCsd0.size(), 0);
            DDD("resending csd0");
            DDD("calling %s success", __func__);
        }
    }

private:
    const std::shared_ptr<C2BaseParams> mParams;
    std::unique_ptr<MediaHevcDecoder> mContext;
    std::unique_ptr<GoldfishHevcHelper> mHevcHelper;
    std::shared_ptr<C2GraphicBlock> mOutBlock;

    std::vector<uint8_t> mCsd0;
    std::vector<uint8_t> mCsd1;

    std::map<uint64_t, uint64_t> mOldPts2Index;
    std::map<uint64_t, uint64_t> mPts2Index;
    std::map<uint64_t, uint64_t> mIndex2Pts;

    uint8_t *mInPBuffer{nullptr};

    GfImage mImg{};
    VuiColorAspects mBitstreamColorAspects;
    MetaDataColorAspects mSentMetadata = {1, 0, 0, 0};

    uint64_t  mPts {0};

    uint32_t mConsumedBytes{0};
    uint32_t mInPBufferSize = 0;

    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    uint32_t mStride = 0;

    int mHostColorBufferId{-1};

    bool mEnableAndroidNativeBuffers{true};
    bool mSignalledOutputEos{false};
    bool mSignalledError{false};
    bool mHeaderDecoded{false};

    C2_DO_NOT_COPY(C2GoldfishHevcDec);
};

} // namespace

std::shared_ptr<const IComponentFactory> getC2GoldfishHevcDecFactory() {
    struct ImplFactory : public IComponentFactory {
        struct HevcParams : public C2BaseParams {
            explicit HevcParams(const std::shared_ptr<C2ReflectorHelper> &helper)
                : C2BaseParams(helper, COMPONENT_NAME, C2Component::KIND_DECODER,
                            C2Component::DOMAIN_VIDEO, ::android::MEDIA_MIMETYPE_VIDEO_HEVC) {
                addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                        .withDefault(std::make_shared<C2StreamProfileLevelInfo::input>(
                            0u, C2Config::PROFILE_HEVC_MAIN, C2Config::LEVEL_HEVC_MAIN_5_1))
                        .withFields({
                            C2F(mProfileLevel, profile).oneOf({
                                    C2Config::PROFILE_HEVC_MAIN,
                                    C2Config::PROFILE_HEVC_MAIN_STILL}),
                            C2F(mProfileLevel, level).oneOf({
                                    C2Config::LEVEL_HEVC_MAIN_1,
                                    C2Config::LEVEL_HEVC_MAIN_2, C2Config::LEVEL_HEVC_MAIN_2_1,
                                    C2Config::LEVEL_HEVC_MAIN_3, C2Config::LEVEL_HEVC_MAIN_3_1,
                                    C2Config::LEVEL_HEVC_MAIN_4, C2Config::LEVEL_HEVC_MAIN_4_1,
                                    C2Config::LEVEL_HEVC_MAIN_5, C2Config::LEVEL_HEVC_MAIN_5_1,
                                    C2Config::LEVEL_HEVC_MAIN_5_2, C2Config::LEVEL_HEVC_HIGH_4,
                                    C2Config::LEVEL_HEVC_HIGH_4_1, C2Config::LEVEL_HEVC_HIGH_5,
                                    C2Config::LEVEL_HEVC_HIGH_5_1, C2Config::LEVEL_HEVC_HIGH_5_2
                            })
                        })
                        .withSetter(ProfileLevelSetter, mSize)
                        .build());
            }
        };

        static std::shared_ptr<C2BaseParams> createHevcParams(
                const std::shared_ptr<C2ReflectorHelper>& reflector) {
            return std::make_shared<HevcParams>(reflector);
        }

        std::pair<c2_status_t, std::shared_ptr<C2Component>> createComponent(
                const std::shared_ptr<C2ReflectorHelper>& reflector) const override {
            return {C2_OK, std::make_shared<C2GoldfishHevcDec>(
                        COMPONENT_NAME, 0, createHevcParams(reflector))};
        }

        std::pair<c2_status_t, std::shared_ptr<C2ComponentInterface>> createInterface(
                const std::shared_ptr<C2ReflectorHelper>& reflector) const override {
            return {C2_OK, std::make_shared<SimpleC2Interface<C2BaseParams>>(
                        COMPONENT_NAME, 0, createHevcParams(reflector))};
        }

        std::string_view getName() const override {
            using namespace std::literals::string_view_literals;
            return "hevcdec"sv;
        }
    };

    return std::make_shared<ImplFactory>();
}

} // namespace goldfish::media::c2
