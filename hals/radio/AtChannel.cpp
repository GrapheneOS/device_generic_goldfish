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

#define FAILURE_DEBUG_PREFIX "AtChannel"

#include <cstring>
#include <unistd.h>

#include "AtChannel.h"
#include "debug.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
namespace {
int sendRequestImpl(const int fd, const char* data, size_t size) {
    while (size > 0) {
        const ssize_t written = ::write(fd, data, size);
        if (written >= 0) {
            data += written;
            size -= written;
        } else if (errno == EINTR) {
            continue;
        } else {
            return FAILURE(errno);
        }
    }

    return 0;
}
}  // namespace

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "AtChannel::RequestPipe"

bool AtChannel::RequestPipe::operator()(const std::string_view request) const {
    int err = sendRequestImpl(mFd, request.data(), request.size());
    if (err == 0) {
        const char kCR = 0x0D;
        err = sendRequestImpl(mFd, &kCR, 1);
    }

    return err == 0;
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "AtChannel"

AtChannel::AtChannel(HostChannelFactory hostChannelFactory,
                     InitSequence initSequence)
        : mHostChannelFactory(std::move(hostChannelFactory))
        , mInitSequence(std::move(initSequence)) {
    mRequestThread = std::thread(&AtChannel::requestLoop, this);
}

AtChannel::~AtChannel() {
    queueRequester({});
    mRequestThread.join();
    mReaderThread.join();
}

void AtChannel::queueRequester(Requester requester) {
    std::lock_guard<std::mutex> lock(mRequestQueueMtx);
    mRequesterQueue.push_back(std::move(requester));
    mRequesterAvailable.notify_one();
}

void AtChannel::addResponseSink(ResponseSink responseSink) {
    std::lock_guard<std::mutex> lock(mResponseSinksMtx);
    mResponseSinks.push_back(std::move(responseSink));
}

void AtChannel::requestLoop() {
    while (true) {
        const Requester requester = getRequester();
        if (requester) {
            if (!requester(getHostChannelPipe())) {
                resetHostChannel();
            }
        } else {
            break;
        }
    }

    resetHostChannel();
}

void AtChannel::readingLoop(const int hostChannelFd) {
    std::vector<char> unconsumed;
    while (receiveResponses(hostChannelFd, &unconsumed)) {}
}

AtChannel::Requester AtChannel::getRequester() {
    std::unique_lock<std::mutex> lock(mRequestQueueMtx);
    while (true) {
        if (!mRequesterQueue.empty()) {
            Requester requester(std::move(mRequesterQueue.front()));
            mRequesterQueue.pop_front();
            return requester;
        } else {
            mRequesterAvailable.wait(lock);
        }
    }
}

void AtChannel::broadcastResponse(const AtResponsePtr& response) {
    mConversation.send(response);

    std::lock_guard<std::mutex> lock(mResponseSinksMtx);

    const auto newEnd = std::remove_if(mResponseSinks.begin(), mResponseSinks.end(),
        [&response](const ResponseSink& responseSink) -> bool {
            return !responseSink(response);
        });

    mResponseSinks.erase(newEnd, mResponseSinks.end());
}

void AtChannel::resetHostChannel(unique_fd fd) {
    std::lock_guard<std::mutex> lock(mHostChannelMtx);
    mHostChannel = std::move(fd);
}

int AtChannel::getHostChannelFd() const {
    std::lock_guard<std::mutex> lock(mHostChannelMtx);
    return mHostChannel.get();
}

AtChannel::RequestPipe AtChannel::getHostChannelPipe() {
    int hostChannelFd = getHostChannelFd();
    if (hostChannelFd < 0) {
        if (mReaderThread.joinable()) {
            mReaderThread.join();
        }

        unique_fd newHostChannel = mHostChannelFactory();
        if (!newHostChannel.ok()) {
            return RequestPipe(FAILURE_V(-1, "%s", "mHostChannelFactory failed"));
        }

        hostChannelFd = newHostChannel.get();
        resetHostChannel(std::move(newHostChannel));

        mReaderThread = std::thread([this, hostChannelFd](){
            readingLoop(hostChannelFd);
            resetHostChannel();
        });

        LOG_ALWAYS_FATAL_IF(!mInitSequence(RequestPipe(hostChannelFd), mConversation),
                            "%s:%d: Can't init the host channel", __func__, __LINE__);
    }

    return RequestPipe(hostChannelFd);
}

bool AtChannel::receiveResponses(const int hostChannelFd,
                                 std::vector<char>* unconsumed) {
    const size_t unconsumedSize = unconsumed->size();
    if (unconsumedSize == 0) {
        char buf[128];
        const int len = ::read(hostChannelFd, buf, sizeof(buf));
        if (len > 0) {
            return receiveResponsesImpl(buf, buf + len, unconsumed);
        } else if (len < 0) {
            const int err = errno;
            if (err == EINTR) {
                return true;
            } else {
                return FAILURE_V(false, "fd=%d, err=%s (%d)",
                                 hostChannelFd, ::strerror(err), err);
            }
        }
    } else {
        const size_t newSize = std::max(unconsumedSize + 1024, unconsumed->capacity());
        unconsumed->resize(newSize);
        const int len = ::read(hostChannelFd, &(*unconsumed)[unconsumedSize],
                               newSize - unconsumedSize);
        if (len > 0) {
            unconsumed->resize(unconsumedSize + len);
            char* begin = unconsumed->data();
            char* end = begin + unconsumedSize + len;
            return receiveResponsesImpl(begin, end, unconsumed);
        } else if (len < 0) {
            const int err = errno;
            if (err == EINTR) {
                return true;
            } else {
                return FAILURE_V(false, "fd=%d, err=%s (%d)",
                                 hostChannelFd, ::strerror(err), err);
            }
        }
    }

    return true;
}

// NOTE: [begin, end) could contain one or more requests,
// the last one might be incomplete
bool AtChannel::receiveResponsesImpl(const char* begin, const char* const end,
                                     std::vector<char>* unconsumed) {
    while (begin < end) {
        const char* next = receiveOneResponse(begin, end);
        if (next == begin) {
            unconsumed->assign(begin, end);
            return true;
        } else if (next == nullptr) {
            RLOGE("Can't parse '%*.*s'", int(end - begin), int(end - begin), begin);
            return false;
        } else {
            begin = next;
        }
    }

    unconsumed->clear();
    return true;
}

const char* AtChannel::receiveOneResponse(const char* const begin, const char* const end) {
    switch (*begin) {
    case '\r':
    case '\n':
        return begin + 1;
    }

    auto [consumed, response] = AtResponse::parse(std::string_view(begin, end - begin));
    if (response) {
        broadcastResponse(response);
    }

    return (consumed >= 0) ? (begin + consumed) : nullptr;
}

AtResponsePtr AtChannel::Conversation::operator()(
        const RequestPipe requestPipe,
        const std::string_view request,
        const AtChannel::Conversation::FilterFunc& filter,
        const AtChannel::Conversation::Duration timeout) {
    std::future<AtResponsePtr> futureResponse;

    {
        std::lock_guard<std::mutex> lock(mMtx);
        mFilter = &filter;
        mSink = decltype(mSink)();
        futureResponse = mSink.get_future();
    }

    if (!requestPipe(request)) {
        std::lock_guard<std::mutex> lock(mMtx);
        mFilter = nullptr;
        return nullptr;
    } else if (futureResponse.wait_for(timeout) == std::future_status::ready) {
        return futureResponse.get();
    } else {
        {
            std::lock_guard<std::mutex> lock(mMtx);
            mFilter = nullptr;
        }

        const int requestLen = request.size();
        return FAILURE_V(nullptr, "Timeout for '%*.*s'",
                         requestLen, requestLen, request.data());
    }
}

AtResponsePtr AtChannel::Conversation::operator()(
        const RequestPipe requestPipe,
        const std::string_view request,
        const FilterFunc& filter) {
    using namespace std::chrono_literals;
    return (*this)(requestPipe, request, filter, 3s);
}

bool AtChannel::Conversation::send(const AtResponsePtr& response) {
    std::lock_guard<std::mutex> lock(mMtx);
    if (mFilter && (*mFilter)(*response)) {
        mFilter = nullptr;
        mSink.set_value(response);
        return true;
    } else {
        return false;
    }
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
