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

#include <android-base/unique_fd.h>
#include <cutils/native_handle.h>
#include <system/graphics.h>

#include <aidl/android/hardware/camera/device/StreamBuffer.h>
#include <aidl/android/hardware/common/NativeHandle.h>

#include "StreamInfoCache.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {

using aidl::android::hardware::common::NativeHandle;
using aidl::android::hardware::camera::device::StreamBuffer;
using aidl::android::hardware::graphics::common::BufferUsage;
using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::PixelFormat;

struct CachedStreamBuffer {
    CachedStreamBuffer(const StreamBuffer& sb, StreamInfo si);
    CachedStreamBuffer(CachedStreamBuffer&&) noexcept;
    ~CachedStreamBuffer();

    int64_t getBufferId() const { return mBufferId; }
    const native_handle_t* getBuffer() const { return mBuffer; }

    void importAcquireFence(const NativeHandle& fence);
    bool waitAcquireFence(unsigned timeoutMs);
    base::unique_fd takeAcquireFence() { return std::move(mAcquireFence); }

    void* lock(BufferUsage lockUsage);
    android_ycbcr lockYCbCr(BufferUsage lockUsage);
    base::unique_fd unlock();

    void markProcesssed() { mProcessed = true; }

    const StreamInfo si;

private:
    int64_t mBufferId;
    base::unique_fd mAcquireFence;
    const native_handle_t* mBuffer;  // owned by this class
    bool mProcessed = false;

    CachedStreamBuffer(const CachedStreamBuffer&) = delete;
    CachedStreamBuffer& operator=(const CachedStreamBuffer&) = delete;
    CachedStreamBuffer& operator=(CachedStreamBuffer&&) = delete;
};

}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
