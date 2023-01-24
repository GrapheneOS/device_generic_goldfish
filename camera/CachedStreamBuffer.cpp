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

#include <algorithm>
#include <inttypes.h>

#include <sync/sync.h>
#include <ui/GraphicBufferMapper.h>

#include "CachedStreamBuffer.h"
#include "aidl_utils.h"
#include "debug.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {

using base::unique_fd;

namespace {
const native_handle_t* importAidlNativeHandle(const NativeHandle& anh) {
    typedef decltype(native_handle_t::version) T;
    std::vector<T> data(sizeof(native_handle_t) / sizeof(T) + anh.fds.size() +
        anh.ints.size());

    native_handle_t* h = native_handle_init(
        reinterpret_cast<char*>(&data[0]), anh.fds.size(), anh.ints.size());
    std::transform(anh.fds.begin(), anh.fds.end(), &h->data[0],
                   [](const ndk::ScopedFileDescriptor& sfd){ return sfd.get(); });
    std::copy(anh.ints.begin(), anh.ints.end(), &h->data[anh.fds.size()]);

    const native_handle_t* importedH = nullptr;
    if (GraphicBufferMapper::get().importBufferNoValidate(h, &importedH) != NO_ERROR) {
        return FAILURE(nullptr);
    }

    return importedH;
}
}  //namespace

CachedStreamBuffer::CachedStreamBuffer(const StreamBuffer& sb, StreamInfo s)
        : si(std::move(s))
        , mBufferId(sb.bufferId)
        , mAcquireFence(utils::importAidlNativeHandleFence(sb.acquireFence))
        , mBuffer(importAidlNativeHandle(sb.buffer)) {
    LOG_ALWAYS_FATAL_IF(!mBufferId);
    LOG_ALWAYS_FATAL_IF(!mBuffer);
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
        LOG_ALWAYS_FATAL_IF(GraphicBufferMapper::get().freeBuffer(mBuffer) != NO_ERROR);
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
    void* mem = nullptr;
    if (GraphicBufferMapper::get().lock(
            mBuffer, static_cast<uint32_t>(lockUsage),
            {0, 0, si.size.width, si.size.height}, &mem) == NO_ERROR) {
        return mem;
    } else {
        return FAILURE(nullptr);
    }
}

android_ycbcr CachedStreamBuffer::lockYCbCr(const BufferUsage lockUsage) {
    LOG_ALWAYS_FATAL_IF(!mBuffer);
    android_ycbcr ycbcr;
    if (GraphicBufferMapper::get().lockYCbCr(
            mBuffer, static_cast<uint32_t>(lockUsage),
            {0, 0, si.size.width, si.size.height}, &ycbcr) == NO_ERROR) {
        return ycbcr;
    } else {
        return FAILURE(android_ycbcr());
    }
}

unique_fd CachedStreamBuffer::unlock() {
    LOG_ALWAYS_FATAL_IF(!mBuffer);
    int fenceFd = -1;
    LOG_ALWAYS_FATAL_IF(GraphicBufferMapper::get().unlockAsync(
        mBuffer, &fenceFd) != NO_ERROR);
    return unique_fd(fenceFd);
}

}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
