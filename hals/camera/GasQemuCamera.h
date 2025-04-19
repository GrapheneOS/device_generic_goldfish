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

#pragma once

#include <vector>

#include <android-base/unique_fd.h>

#include "BaseQemuCamera.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {
namespace hw {

struct GasQemuCamera : public BaseQemuCamera {
    explicit GasQemuCamera(const Parameters& params);

    bool configure(const CameraMetadata& sessionParams, size_t nStreams,
                   const Stream* streams, const HalStream* halStreams) override;
    void close() override;

    std::tuple<int64_t, int64_t, CameraMetadata, std::vector<StreamBuffer>,
               std::vector<DelayedStreamBuffer>>
        processCaptureRequest(CameraMetadata, Span<CachedStreamBuffer*>) override;

private:
    struct StreamInfo {
        int32_t id;
        uint32_t blobBufferSize;
        PixelFormat format;
        Rect<uint16_t> size;
    };

    void captureFrame(const StreamInfo& si,
                      CachedStreamBuffer* csb,
                      std::vector<StreamBuffer>* outputBuffers,
                      std::vector<DelayedStreamBuffer>* delayedOutputBuffers) const;
    bool captureFrameYUV(const StreamInfo& si, CachedStreamBuffer* dst) const;
    bool captureFrameRGBA(const StreamInfo& si, CachedStreamBuffer* dst) const;
    DelayedStreamBuffer captureFrameRAW16(const StreamInfo& si,
                                          CachedStreamBuffer* csb) const;
    DelayedStreamBuffer captureFrameJpeg(const StreamInfo& si,
                                         CachedStreamBuffer* csb) const;
    const native_handle_t* captureFrameForCompressing(Rect<uint16_t> dim,
                                                      PixelFormat bufferFormat,
                                                      uint32_t qemuFormat) const;
    bool queryFrame(Rect<uint16_t> dim, uint32_t pixelFormat,
                    float exposureComp, uint64_t dataOffset) const;

    std::vector<StreamInfo> mStreams;
    base::unique_fd mQemuChannel;
};

}  // namespace hw
}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
