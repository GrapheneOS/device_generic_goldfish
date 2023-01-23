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

#pragma once

#include <stdint.h>
#include <unordered_map>

#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/Dataspace.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>

#include "Rect.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {

using aidl::android::hardware::graphics::common::BufferUsage;
using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::PixelFormat;

struct StreamInfo {
    BufferUsage usage;
    Dataspace dataspace;
    PixelFormat pixelFormat;
    Rect<uint16_t> size;
    uint32_t bufferSize;
    int32_t id;
};

using StreamInfoCache = std::unordered_map<int32_t, StreamInfo>;

}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
