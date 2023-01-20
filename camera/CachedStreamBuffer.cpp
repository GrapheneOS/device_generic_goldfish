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

#include <inttypes.h>

#include <aidlcommonsupport/NativeHandle.h>
#include <HandleImporter.h>
#include <sync/sync.h>

#include "CachedStreamBuffer.h"
#include "aidl_utils.h"
#include "debug.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {

using aidl::android::hardware::common::NativeHandle;
using android::hardware::camera::common::V1_0::helper::HandleImporter;
using base::unique_fd;

namespace {
HandleImporter g_importer;
}  // namespace

CachedStreamBuffer::CachedStreamBuffer(const StreamBuffer& sb, StreamInfo s)
        : si(std::move(s))
        , mBufferId(sb.bufferId)
        , mAcquireFence(utils::importAidlNativeHandleFence(sb.acquireFence))
        , mBuffer(dupFromAidl(sb.buffer)) {
    LOG_ALWAYS_FATAL_IF(!mBufferId);
    LOG_ALWAYS_FATAL_IF(!mBuffer);
    g_importer.importBuffer(mBuffer);
}

CachedStreamBuffer::CachedStreamBuffer(CachedStreamBuffer&& rhs) noexcept
        : si(rhs.si)
        , mBufferId(std::exchange(rhs.mBufferId, 0))
        , mAcquireFence(std::exchange(rhs.mAcquireFence, {}))
        , mBuffer(std::exchange(rhs.mBuffer, nullptr))
        , mProcessed(std::exchange(rhs.mProcessed, true)) {
    LOG_ALWAYS_FATAL_IF(!mBufferId);
    LOG_ALWAYS_FATAL_IF(!mBuffer);
}

CachedStreamBuffer::~CachedStreamBuffer() {
    LOG_ALWAYS_FATAL_IF(!mProcessed);
    if (mBufferId) {
        LOG_ALWAYS_FATAL_IF(!mBuffer);
        g_importer.freeBuffer(mBuffer);
    }
}

void CachedStreamBuffer::importAcquireFence(const NativeHandle& fence) {
    LOG_ALWAYS_FATAL_IF(!mProcessed);
    mAcquireFence = utils::importAidlNativeHandleFence(fence);
    mProcessed = false;
}

bool CachedStreamBuffer::waitAcquireFence(const unsigned timeoutMs) {
    if (mAcquireFence.ok()) {
        if (sync_wait(mAcquireFence.get(), timeoutMs)) {
            return FAILURE(false);
        } else {
            mAcquireFence.reset();
            return true;
        }
    } else {
        return true;
    }
}

void* CachedStreamBuffer::lock(const BufferUsage lockUsage) {
    LOG_ALWAYS_FATAL_IF(!mBuffer);
    void* mem = g_importer.lock(mBuffer, static_cast<uint64_t>(lockUsage),
                                {0, 0, si.size.width, si.size.height});
    return mem ? mem : FAILURE(mem);
}

android_ycbcr CachedStreamBuffer::lockYCbCr(const BufferUsage lockUsage) {
    LOG_ALWAYS_FATAL_IF(!mBuffer);
    auto ycbcr = g_importer.lockYCbCr(mBuffer, static_cast<uint64_t>(lockUsage),
                                      {0, 0, si.size.width, si.size.height});

    android_ycbcr aycbcr;
    aycbcr.y = ycbcr.y;
    aycbcr.cb = ycbcr.cb;
    aycbcr.cr = ycbcr.cr;
    aycbcr.ystride = ycbcr.yStride;
    aycbcr.cstride = ycbcr.cStride;
    aycbcr.chroma_step = ycbcr.chromaStep;

    return aycbcr;
}

unique_fd CachedStreamBuffer::unlock() {
    LOG_ALWAYS_FATAL_IF(!mBuffer);
    return unique_fd(g_importer.unlock(mBuffer));
}

}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
