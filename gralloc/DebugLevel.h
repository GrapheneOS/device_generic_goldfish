/*
* Copyright 2024 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#pragma once
#include <android-base/properties.h>

enum class DebugLevel {
    ERROR = 0,
    ALLOC = 1,
    IMPORT = 2,
    LOCK = 3,
    FLUSH = 4,
    METADATA = 5,
};

inline DebugLevel getDebugLevel() {
    return static_cast<DebugLevel>(
        ::android::base::GetIntProperty("ro.boot.qemu.gralloc.debug_level",
                                        static_cast<int>(DebugLevel::ERROR)));
}
