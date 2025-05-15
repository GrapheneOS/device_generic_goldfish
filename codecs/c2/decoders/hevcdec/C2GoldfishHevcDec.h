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

#ifndef ANDROID_C2_SOFT_HEVC_DEC_H_
#define ANDROID_C2_SOFT_HEVC_DEC_H_

#include <sys/time.h>

#include <media/stagefright/foundation/ColorUtils.h>

#include "MediaHevcDecoder.h"
#include "GoldfishHevcHelper.h"
#include <SimpleC2Component.h>
#include <atomic>
#include <map>

namespace android {

#define ALIGN2(x) ((((x) + 1) >> 1) << 1)
#define ALIGN8(x) ((((x) + 7) >> 3) << 3)
#define ALIGN16(x) ((((x) + 15) >> 4) << 4)
#define ALIGN32(x) ((((x) + 31) >> 5) << 5)
#define MAX_NUM_CORES 4
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define GETTIME(a, b) gettimeofday(a, b);
#define TIME_DIFF(start, end, diff)                                            \
    diff = (((end).tv_sec - (start).tv_sec) * 1000000) +                       \
           ((end).tv_usec - (start).tv_usec);

class C2GoldfishHevcDec : public SimpleC2Component {
  public:
    class IntfImpl;
    C2GoldfishHevcDec(const char *name, c2_node_id_t id,
                     const std::shared_ptr<IntfImpl> &intfImpl);
    virtual ~C2GoldfishHevcDec();

    // From SimpleC2Component
    c2_status_t onInit() override;
    c2_status_t onStop() override;
    void onReset() override;
    void onRelease() override;
    c2_status_t onFlush_sm() override;
    void process(const std::unique_ptr<C2Work> &work,
                 const std::shared_ptr<C2BlockPool> &pool) override;
    c2_status_t drain(uint32_t drainMode,
                      const std::shared_ptr<C2BlockPool> &pool) override;

  private:
    void checkMode(const std::shared_ptr<C2BlockPool> &pool);
    //    status_t createDecoder();
    status_t createDecoder();
    status_t setParams(size_t stride);
    status_t initDecoder();
    bool setDecodeArgs(C2ReadView *inBuffer, C2GraphicView *outBuffer,
                       size_t inOffset, size_t inSize, uint32_t tsMarker, bool hasPicture);
    c2_status_t ensureDecoderState(const std::shared_ptr<C2BlockPool> &pool);
    void finishWork(uint64_t index, const std::unique_ptr<C2Work> &work);
    status_t setFlushMode();
    c2_status_t drainInternal(uint32_t drainMode,
                              const std::shared_ptr<C2BlockPool> &pool,
                              const std::unique_ptr<C2Work> &work);
    status_t resetDecoder();
    void resetPlugin();
    void deleteContext();

    void removePts(uint64_t pts);
    void insertPts(uint32_t work_index, uint64_t pts);
    uint64_t getWorkIndex(uint64_t pts);

    // TODO:This is not the right place for this enum. These should
    // be part of c2-vndk so that they can be accessed by all video plugins
    // until then, make them feel at home
    enum {
        kNotSupported,
        kPreferBitstream,
        kPreferContainer,
    };

    void getVuiParams(hevc_image_t &img);
    void copyImageData(hevc_image_t &img);




    // Color aspects. These are ISO values and are meant to detect changes in
    // aspects to avoid converting them to C2 values for each frame
    struct VuiColorAspects {
        uint8_t primaries;
        uint8_t transfer;
        uint8_t coeffs;
        uint8_t fullRange;

        // default color aspects
        VuiColorAspects()
            : primaries(2), transfer(2), coeffs(2), fullRange(0) {}

        bool operator==(const VuiColorAspects &o) const {
            return primaries == o.primaries && transfer == o.transfer &&
                   coeffs == o.coeffs && fullRange == o.fullRange;
        }
    };

    void sendMetadata();

    void decodeHeaderAfterFlush();

    std::unique_ptr<MediaHevcDecoder> mContext;
    std::unique_ptr<GoldfishHevcHelper> mHevcHelper;
    std::shared_ptr<IntfImpl> mIntf;
    std::shared_ptr<C2GraphicBlock> mOutBlock;

    std::vector<uint8_t> mCsd0;
    std::vector<uint8_t> mCsd1;

    std::map<uint64_t, uint64_t> mOldPts2Index;
    std::map<uint64_t, uint64_t> mPts2Index;
    std::map<uint64_t, uint64_t> mIndex2Pts;

    uint8_t *mInPBuffer{nullptr};
    uint8_t *mOutBufferFlush{nullptr};

    hevc_image_t mImg{};
    VuiColorAspects mBitstreamColorAspects;
    MetaDataColorAspects mSentMetadata = {1, 0, 0, 0};

    std::atomic_uint64_t mOutIndex;
    // there are same pts matching to different work indices
    // this happen during csd0/csd1 switching
    uint64_t  mPts {0};

    uint32_t mConsumedBytes{0};
    uint32_t mInPBufferSize = 0;
    uint32_t mInTsMarker = 0;

    // size_t mNumCores;
    // uint32_t mOutputDelay;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    uint32_t mStride = 0;

    int mHostColorBufferId{-1};

    bool mEnableAndroidNativeBuffers{true};
    bool mSignalledOutputEos{false};
    bool mSignalledError{false};
    bool mHeaderDecoded{false};

    // profile
    struct timeval mTimeStart;
    struct timeval mTimeEnd;
#ifdef FILE_DUMP_ENABLE
    char mInFile[200];
#endif /* FILE_DUMP_ENABLE */

    C2_DO_NOT_COPY(C2GoldfishHevcDec);
};

} // namespace android

#endif // ANDROID_C2_SOFT_HEVC_DEC_H_
