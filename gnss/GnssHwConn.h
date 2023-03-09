/*
 * Copyright (C) 2020 The Android Open Source Project
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
#include <future>
#include <mutex>
#include <thread>
#include <android/hardware/gnss/2.0/IGnssCallback.h>

namespace goldfish {
using ::android::base::unique_fd;
using ::android::hardware::gnss::V2_0::IGnssCallback;
using ::android::sp;

class GnssHwConn {
public:
    explicit GnssHwConn(sp<IGnssCallback> callback);
    ~GnssHwConn();

    bool ok() const;
    bool start();
    bool stop();

private:
    bool sendWorkerThreadCommand(char cmd) const;

    unique_fd mDevFd;      // Goldfish GPS QEMU device
    unique_fd mCallersFd;  // a channel to talk to the thread
    std::thread mThread;
};

}  // namespace goldfish
