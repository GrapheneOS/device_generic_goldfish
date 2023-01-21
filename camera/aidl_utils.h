/*
 * Copyright (C) 2023 The Android Open Source Project
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
#include <aidl/android/hardware/camera/device/BufferStatus.h>
#include <aidl/android/hardware/camera/device/CameraMetadata.h>
#include <aidl/android/hardware/camera/device/CaptureResult.h>
#include <aidl/android/hardware/camera/device/StreamBuffer.h>
#include <aidl/android/hardware/common/NativeHandle.h>

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {
namespace utils {

using aidl::android::hardware::camera::device::CameraMetadata;
using aidl::android::hardware::camera::device::CaptureResult;
using aidl::android::hardware::camera::device::StreamBuffer;
using aidl::android::hardware::common::NativeHandle;

base::unique_fd importAidlNativeHandleFence(const NativeHandle& nh);

StreamBuffer makeStreamBuffer(int streamId, int64_t bufferId, bool success,
                              base::unique_fd releaseFence);

CaptureResult makeCaptureResult(int frameNumber, CameraMetadata metadata,
                                std::vector<StreamBuffer> outputBuffers);

}  // namespace utils
}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
