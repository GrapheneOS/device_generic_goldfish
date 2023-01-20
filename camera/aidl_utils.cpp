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

#include <fcntl.h>

#include <aidlcommonsupport/NativeHandle.h>

#include "debug.h"
#include "aidl_utils.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {
namespace utils {

using aidl::android::hardware::camera::device::BufferStatus;
using base::unique_fd;

namespace {
NativeHandle moveFenceToAidlNativeHandle(unique_fd fence) {
    if (!fence.ok()) {
        return {};
    }

    typedef decltype(native_handle_t::version) T;
    T on_stack[sizeof(native_handle_t) / sizeof(T) + 1];

    native_handle_t* nh = native_handle_init(
        reinterpret_cast<char*>(&on_stack[0]), 1, 0);
    nh->data[0] = fence.release();
    return makeToAidl(nh);
}
}  // namespace

unique_fd importAidlNativeHandleFence(const NativeHandle& nh) {
    const size_t nfds = nh.fds.size();
    const size_t nints = nh.ints.size();

    if (nints == 0) {
        switch (nfds) {
        case 0:
            return unique_fd();

        case 1: {
                const int fd = fcntl(nh.fds.front().get(), F_DUPFD_CLOEXEC, 0);
                if (fd < 0) {
                    return FAILURE_V(unique_fd(), "fcntl failed with %s (%d)",
                                     strerror(errno), errno);
                }

                return unique_fd(fd);
            }

        default:
            return FAILURE_V(unique_fd(), "unexpected fence shape, nfds=%zu, must "
                             "be one", nfds);
        }
    } else {
        return unique_fd();
    }
}

StreamBuffer makeStreamBuffer(const int streamId,
                              const int64_t bufferId,
                              const bool success,
                              base::unique_fd releaseFence) {
    StreamBuffer sb;
    sb.streamId = streamId;
    sb.bufferId = bufferId;
    sb.status = success ? BufferStatus::OK : BufferStatus::ERROR;
    sb.releaseFence = moveFenceToAidlNativeHandle(std::move(releaseFence));
    return sb;
}

CaptureResult makeCaptureResult(const int frameNumber,
                                CameraMetadata metadata,
                                std::vector<StreamBuffer> outputBuffers) {
    CaptureResult cr;
    cr.frameNumber = frameNumber;
    cr.result = std::move(metadata);
    cr.outputBuffers = std::move(outputBuffers);
    cr.inputBuffer.streamId = -1;
    cr.inputBuffer.bufferId = 0;
    cr.partialResult = cr.result.metadata.empty() ? 0 : 1;
    return cr;
}

}  // namespace utils
}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
