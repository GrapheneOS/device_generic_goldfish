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

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include <android-base/unique_fd.h>

#include "AtResponse.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
using ::android::base::unique_fd;

struct AtChannel {
    using HostChannelFactory = std::function<unique_fd()>;

    struct RequestPipe {
        explicit RequestPipe(int fd) : mFd(fd) {}

        bool operator()(std::string_view request) const;

        RequestPipe(const RequestPipe&) = default;
        RequestPipe& operator=(const RequestPipe&) = default;

    private:
        int mFd;
    };

    struct Conversation {
        using FilterFunc = std::function<bool(const AtResponse&)>;
        using Duration = std::chrono::steady_clock::duration;

        AtResponsePtr operator()(RequestPipe, std::string_view request,
                                 const FilterFunc& filter, Duration timeout);
        AtResponsePtr operator()(RequestPipe, std::string_view request,
                                 const FilterFunc& filter);

        bool send(const AtResponsePtr& response);

    private:
        const FilterFunc* mFilter = nullptr;
        std::promise<AtResponsePtr> mSink;
        mutable std::mutex mMtx;
    };

    using InitSequence = std::function<bool(RequestPipe, Conversation&)>;
    using Requester = std::function<bool(RequestPipe)>;
    using ResponseSink = std::function<bool(const AtResponsePtr&)>;

    AtChannel(HostChannelFactory hostChannelFactory,
              InitSequence initSequence);
    ~AtChannel();

    void queueRequester(Requester);
    void addResponseSink(ResponseSink);

private:
    void requestLoop();
    void readingLoop(int fd);
    Requester getRequester();
    void broadcastResponse(const AtResponsePtr&);

    void resetHostChannel(unique_fd fd = {});
    int getHostChannelFd() const;
    RequestPipe getHostChannelPipe();
    bool receiveResponses(int hostChannel, std::vector<char>* unconsumed);
    bool receiveResponsesImpl(const char* begin, const char* end,
                              std::vector<char>* unconsumed);
    const char* receiveOneResponse(const char* begin, const char* end);

    const HostChannelFactory mHostChannelFactory;
    const InitSequence mInitSequence;
    Conversation mConversation;
    unique_fd mHostChannel;
    std::deque<Requester> mRequesterQueue;
    std::condition_variable mRequesterAvailable;
    std::vector<ResponseSink> mResponseSinks;
    std::vector<char> mResponseBuf;
    std::thread mRequestThread;
    std::thread mReaderThread;
    mutable std::mutex mRequestQueueMtx;
    mutable std::mutex mResponseSinksMtx;
    mutable std::mutex mHostChannelMtx;
};

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
