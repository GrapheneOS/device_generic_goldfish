/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <linux/videodev2.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>

#include <gralloc_cb_bp.h>

#include "GasQemuCamera.h"

#include "debug.h"
#include "jpeg.h"
#include "qemu_channel.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {
namespace hw {
namespace {
constexpr char kClass[] = "GasQemuCamera";

constexpr BufferUsage usageOr(const BufferUsage a, const BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}
}  // namespace

GasQemuCamera::GasQemuCamera(const Parameters& params)
        : BaseQemuCamera(params)
{}

bool GasQemuCamera::configure(const CameraMetadata& sessionParams,
                              const size_t nStreams,
                              const Stream* streams,
                              const HalStream* halStreams) {
    if (!mQemuChannel.ok()) {
        auto qemuChannel = qemuOpenChannel(std::string("name=") + mParams.name);
        if (!qemuChannel.ok()) {
            return false;
        }
        static const char kConnectQuery[] = "connect";
        if (qemuRunQuery(qemuChannel.get(), kConnectQuery, sizeof(kConnectQuery)) < 0) {
            return false;
        }
        static const char kStartQuery[] = "start";
        if (qemuRunQuery(qemuChannel.get(), kStartQuery, sizeof(kStartQuery)) < 0) {
            return false;
        }
        mQemuChannel = std::move(qemuChannel);
    }

    mStreams.resize(nStreams);
    for (size_t i = 0; i < nStreams; ++i, ++streams, ++halStreams) {
        LOG_ALWAYS_FATAL_IF(streams->id != halStreams->id);
        StreamInfo& si = mStreams[i];
        si.id = streams->id;
        si.size.width = streams->width;
        si.size.height = streams->height;
        si.blobBufferSize = streams->bufferSize;
        si.format = halStreams->overrideFormat;
    }

    applyMetadata(sessionParams);
    return true;
}

void GasQemuCamera::close() {
    if (mQemuChannel.ok()) {
        static const char kStopQuery[] = "stop";
        if (qemuRunQuery(mQemuChannel.get(), kStopQuery, sizeof(kStopQuery)) >= 0) {
            static const char kDisconnectQuery[] = "disconnect";
            qemuRunQuery(mQemuChannel.get(), kDisconnectQuery, sizeof(kDisconnectQuery));
        }
        mQemuChannel.reset();
    }
    mStreams.clear();
}

std::tuple<int64_t, int64_t, CameraMetadata,
           std::vector<StreamBuffer>, std::vector<DelayedStreamBuffer>>
GasQemuCamera::processCaptureRequest(CameraMetadata metadataUpdate,
                                  Span<CachedStreamBuffer*> csbs) {
    CameraMetadata resultMetadata = metadataUpdate.metadata.empty() ?
        updateCaptureResultMetadata() :
        applyMetadata(std::move(metadataUpdate));

    const size_t csbsSize = csbs.size();
    std::vector<StreamBuffer> outputBuffers;
    std::vector<DelayedStreamBuffer> delayedOutputBuffers;
    outputBuffers.reserve(csbsSize);

    for (size_t i = 0; i < csbsSize; ++i) {
        CachedStreamBuffer* csb = csbs[i];
        LOG_ALWAYS_FATAL_IF(!csb);  // otherwise mNumBuffersInFlight will be hard

        const StreamInfo* si = csb->getStreamInfo<StreamInfo>();
        if (!si) {
            const int32_t id = csb->getStreamId();
            const auto sii =
                std::find_if(mStreams.begin(), mStreams.end(),
                             [id](const StreamInfo& si){
                                 return id == si.id;
                             });

            if (sii == mStreams.end()) {
                ALOGE("%s:%s:%d could not find stream=%d in the cache",
                      kClass, __func__, __LINE__, csb->getStreamId());
            } else {
                si = &*sii;
                csb->setStreamInfo(si);
            }
        }

        if (si) {
            captureFrame(*si, csb, &outputBuffers, &delayedOutputBuffers);
        } else {
            outputBuffers.push_back(csb->finish(false));
        }
    }

    return make_tuple((mQemuChannel.ok() ? mFrameDurationNs : FAILURE(-1)),
                      mSensorExposureDurationNs,
                      std::move(resultMetadata), std::move(outputBuffers),
                      std::move(delayedOutputBuffers));
}

void GasQemuCamera::captureFrame(const StreamInfo& si,
                                 CachedStreamBuffer* csb,
                                 std::vector<StreamBuffer>* outputBuffers,
                                 std::vector<DelayedStreamBuffer>* delayedOutputBuffers) const {
    switch (si.format) {
    case PixelFormat::YCBCR_420_888:
        outputBuffers->push_back(csb->finish(captureFrameYUV(si, csb)));
        break;
    case PixelFormat::RGBA_8888:
        outputBuffers->push_back(csb->finish(captureFrameRGBA(si, csb)));
        break;
    case PixelFormat::RAW16:
        delayedOutputBuffers->push_back(captureFrameRAW16(si, csb));
        break;
    case PixelFormat::BLOB:
        delayedOutputBuffers->push_back(captureFrameJpeg(si, csb));
        break;
    default:
        ALOGE("%s:%s:%d: unexpected format=%s", kClass,
              __func__, __LINE__, toString(si.format).c_str());
        outputBuffers->push_back(csb->finish(false));
        break;
    }
}

bool GasQemuCamera::captureFrameYUV(const StreamInfo& si,
                                    CachedStreamBuffer* csb) const {
    if (!csb->waitAcquireFence(mFrameDurationNs / 2000000)) {
        return FAILURE(false);
    }

    const cb_handle_t* const cb = cb_handle_t::from(csb->getBuffer());
    if (!cb) {
        return FAILURE(false);
    }

    const auto size = si.size;
    android_ycbcr ycbcr;
    if (GraphicBufferMapper::get().lockYCbCr(
            cb, static_cast<uint32_t>(BufferUsage::CPU_WRITE_OFTEN),
            {size.width, size.height}, &ycbcr) != NO_ERROR) {
        return FAILURE(false);
    }

    bool const res = queryFrame(si.size, V4L2_PIX_FMT_YUV420,
                                mExposureComp, cb->getMmapedOffset());

    LOG_ALWAYS_FATAL_IF(GraphicBufferMapper::get().unlock(cb) != NO_ERROR);
    return res;
}

bool GasQemuCamera::captureFrameRGBA(const StreamInfo& si,
                                     CachedStreamBuffer* csb) const {
    if (!csb->waitAcquireFence(mFrameDurationNs / 2000000)) {
        return FAILURE(false);
    }

    const cb_handle_t* const cb = cb_handle_t::from(csb->getBuffer());
    if (!cb) {
        return FAILURE(false);
    }

    const auto size = si.size;
    void* mem = nullptr;
    if (GraphicBufferMapper::get().lock(
            cb, static_cast<uint32_t>(BufferUsage::CPU_WRITE_OFTEN),
            {size.width, size.height}, &mem) != NO_ERROR) {
        return FAILURE(false);
    }

    bool const res = queryFrame(si.size, V4L2_PIX_FMT_RGB32,
                                mExposureComp, cb->getMmapedOffset());

    LOG_ALWAYS_FATAL_IF(GraphicBufferMapper::get().unlock(cb) != NO_ERROR);
    return res;
}

DelayedStreamBuffer GasQemuCamera::captureFrameRAW16(const StreamInfo& si,
                                                     CachedStreamBuffer* csb) const {
    const native_handle_t* const image = captureFrameForCompressing(
        si.size, PixelFormat::RGBA_8888, V4L2_PIX_FMT_RGB32);

    const Rect<uint16_t> imageSize = si.size;
    const int64_t frameDurationNs = mFrameDurationNs;
    CameraMetadata metadata = mCaptureResultMetadata;

    return [csb, image, imageSize, metadata = std::move(metadata),
            frameDurationNs](const bool ok) -> StreamBuffer {
        StreamBuffer sb;
        if (ok && image && csb->waitAcquireFence(frameDurationNs / 1000000)) {
            void* mem = nullptr;
            if (GraphicBufferMapper::get().lock(
                    image, static_cast<uint32_t>(BufferUsage::CPU_READ_OFTEN),
                    {imageSize.width, imageSize.height}, &mem) == NO_ERROR) {
                sb = csb->finish(convertRGBAtoRAW16(imageSize, mem, csb->getBuffer()));
                LOG_ALWAYS_FATAL_IF(GraphicBufferMapper::get().unlock(image) != NO_ERROR);
            } else {
                sb = csb->finish(FAILURE(false));
            }
        } else {
            sb = csb->finish(false);
        }
        if (image) {
            GraphicBufferAllocator::get().free(image);
        }
        return sb;
    };
}
DelayedStreamBuffer GasQemuCamera::captureFrameJpeg(const StreamInfo& si,
                                                    CachedStreamBuffer* csb) const {
    const native_handle_t* const image = captureFrameForCompressing(
        si.size, PixelFormat::YCBCR_420_888, V4L2_PIX_FMT_YUV420);

    const Rect<uint16_t> imageSize = si.size;
    const uint32_t jpegBufferSize = si.blobBufferSize;
    const int64_t frameDurationNs = mFrameDurationNs;
    CameraMetadata metadata = mCaptureResultMetadata;

    return [csb, image, imageSize, metadata = std::move(metadata), jpegBufferSize,
            frameDurationNs](const bool ok) -> StreamBuffer {
        StreamBuffer sb;
        if (ok && image && csb->waitAcquireFence(frameDurationNs / 1000000)) {
            android_ycbcr imageYcbcr;
            if (GraphicBufferMapper::get().lockYCbCr(
                    image, static_cast<uint32_t>(BufferUsage::CPU_READ_OFTEN),
                    {imageSize.width, imageSize.height}, &imageYcbcr) == NO_ERROR) {
                sb = csb->finish(compressJpeg(imageSize, imageYcbcr, metadata,
                                              csb->getBuffer(), jpegBufferSize));
                LOG_ALWAYS_FATAL_IF(GraphicBufferMapper::get().unlock(image) != NO_ERROR);
            } else {
                sb = csb->finish(FAILURE(false));
            }
        } else {
            sb = csb->finish(false);
        }

        if (image) {
            GraphicBufferAllocator::get().free(image);
        }
        return sb;
    };
}

const native_handle_t* GasQemuCamera::captureFrameForCompressing(
        const Rect<uint16_t> dim,
        const PixelFormat bufferFormat,
        const uint32_t qemuFormat) const {
    constexpr BufferUsage kUsage = usageOr(BufferUsage::CAMERA_OUTPUT,
                                           BufferUsage::CPU_READ_OFTEN);

    GraphicBufferAllocator& gba = GraphicBufferAllocator::get();

    const native_handle_t* image = nullptr;
    uint32_t stride;
    if (gba.allocate(dim.width, dim.height, static_cast<int>(bufferFormat), 1,
                     static_cast<uint64_t>(kUsage), &image, &stride,
                     "GasQemuCamera") != NO_ERROR) {
        return FAILURE(nullptr);
    }

    const cb_handle_t* const cb = cb_handle_t::from(image);
    if (!cb) {
        gba.free(image);
        return FAILURE(nullptr);
    }

    if (!queryFrame(dim, qemuFormat, mExposureComp, cb->getMmapedOffset())) {
        gba.free(image);
        return FAILURE(nullptr);
    }

    return image;
}

bool GasQemuCamera::queryFrame(const Rect<uint16_t> dim,
                               const uint32_t pixelFormat,
                               const float exposureComp,
                               const uint64_t dataOffset) const {
    char queryStr[128];
    const int querySize = snprintf(queryStr, sizeof(queryStr),
        "frame dim=%" PRIu32 "x%" PRIu32 " pix=%" PRIu32 " offset=%" PRIu64
        " expcomp=%g", dim.width, dim.height, static_cast<uint32_t>(pixelFormat),
        dataOffset, exposureComp);

    return qemuRunQuery(mQemuChannel.get(), queryStr, querySize + 1) >= 0;
}

}  // namespace hw
}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
