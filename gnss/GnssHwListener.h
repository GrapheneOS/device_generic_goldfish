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
#include <chrono>
#include <optional>
#include <vector>
#include <android/hardware/gnss/1.0/types.h>
#include <android/hardware/gnss/2.0/IGnssCallback.h>
#include <android/hardware/gnss/2.0/types.h>

namespace goldfish {
namespace ahg = ::android::hardware::gnss;
namespace ahg20 = ahg::V2_0;
namespace ahg10 = ahg::V1_0;

using ahg20::IGnssCallback;

class GnssHwListener {
public:
    explicit GnssHwListener(IGnssCallback& callback);
    ~GnssHwListener();

    void start();
    void stop();
    void consume(const char* buf, size_t size);

    GnssHwListener(const GnssHwListener&) = delete;
    GnssHwListener(GnssHwListener&&) = delete;
    GnssHwListener& operator=(const GnssHwListener&) = delete;
    GnssHwListener& operator=(GnssHwListener&&) = delete;

private:
    void consume1(char);
    bool parse(const char* begin, const char* end,
               const ahg10::GnssUtcTime& t,
               const ahg20::ElapsedRealtime& ert);
    bool parseGPRMC(const char* begin, const char* end,
                    const ahg10::GnssUtcTime& t,
                    const ahg20::ElapsedRealtime& ert);
    bool parseGPGGA(const char* begin, const char* end,
                    const ahg10::GnssUtcTime& t,
                    const ahg20::ElapsedRealtime& ert);
    bool isWarmedUp() const;

    IGnssCallback&        mCallback;
    std::optional<std::chrono::steady_clock::time_point> mWarmedUp;
    std::vector<char>     mBuffer;
    std::optional<double> mAltitude;
};

}  // namespace goldfish
