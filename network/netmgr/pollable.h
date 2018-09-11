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

#pragma once

#include <chrono>

class Pollable {
public:
    using Clock = std::chrono::steady_clock;
    using Timestamp = Clock::time_point;
    struct Data {
        int fd;
        Timestamp deadline;
    };
    virtual ~Pollable() = default;

    virtual Data data() const = 0;
    virtual void onReadAvailable() = 0;
    virtual void onClose() = 0;
    virtual void onTimeout() = 0;
};

