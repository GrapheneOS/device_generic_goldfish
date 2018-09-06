/*
 * Copyright 2018, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "poller.h"

#include "log.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <vector>

using std::chrono::duration_cast;

static struct timespec* getTimeout(Pollable::Timestamp deadline,
                                   struct timespec* ts) {
    if (deadline < Pollable::Timestamp::max()) {
        auto timeout = deadline - Pollable::Clock::now();
        // Convert and round down to seconds
        auto seconds = duration_cast<std::chrono::seconds>(timeout);
        // Then subtract the seconds from the timeout and convert the remainder
        auto nanos = duration_cast<std::chrono::nanoseconds>(timeout - seconds);

        ts->tv_sec = seconds.count();
        ts->tv_nsec = nanos.count();

        return ts;
    }
    return nullptr;
}

Poller::Poller() {
}

void Poller::addPollable(Pollable* pollable) {
    mPollables.push_back(pollable);
}

int Poller::run() {
    // Block all signals while we're running. This way we don't have to deal
    // with things like EINTR. We then uses ppoll to set the original mask while
    // polling. This way polling can be interrupted but socket writing, reading
    // and ioctl remain interrupt free. If a signal arrives while we're blocking
    // it it will be placed in the signal queue and handled once ppoll sets the
    // original mask. This way no signals are lost.
    sigset_t blockMask, mask;
    int status = ::sigfillset(&blockMask);
    if (status != 0) {
        LOGE("Unable to fill signal set: %s", strerror(errno));
        return errno;
    }
    status = ::sigprocmask(SIG_SETMASK, &blockMask, &mask);
    if (status != 0) {
        LOGE("Unable to set signal mask: %s", strerror(errno));
        return errno;
    }

    std::vector<struct pollfd> fds;
    while (true) {
        fds.clear();
        Pollable::Timestamp deadline = Pollable::Timestamp::max();
        for (const auto& pollable : mPollables) {
            int fd = pollable->data().fd;
            if (fd != -1) {
                fds.push_back(pollfd{});
                fds.back().fd = fd;
                fds.back().events = POLLIN;
            }
            if (pollable->data().deadline < deadline) {
                deadline = pollable->data().deadline;
            }
        }

        struct timespec ts = { 0, 0 };
        struct timespec* tsPtr = getTimeout(deadline, &ts);
        status = ::ppoll(fds.data(), fds.size(), tsPtr, &mask);
        if (status < 0) {
            if (errno == EINTR) {
                // Interrupted, just keep going
                continue;
            }
            // Actual error, time to quit
            LOGE("Polling failed: %s", strerror(errno));
            return errno;
        }
        // Check for timeouts
        Pollable::Timestamp now = Pollable::Clock::now();
        for (auto& pollable : mPollables) {
            // Since we're going to have a very low number of pollables it's
            // probably faster to just loop through fds here instead of
            // constructing a map from fd to pollable every polling loop.
            for (const auto& fd : fds) {
                if (fd.fd == pollable->data().fd) {
                    if (fd.revents & POLLIN) {
                        // This pollable has data available for reading
                        pollable->onReadAvailable();
                    }
                    if (fd.revents & POLLHUP) {
                        // The fd was closed from the other end
                        pollable->onClose();
                    }
                }
            }
            // Potentially trigger both read and timeout for pollables that have
            // different logic for these two cases. By checking the timeout
            // after the read we allow the pollable to update the deadline after
            // the read to prevent this from happening.
            if (now > pollable->data().deadline) {
                // This pollable has reached its deadline
                pollable->onTimeout();
            }
        }
    }

    return 0;
}
