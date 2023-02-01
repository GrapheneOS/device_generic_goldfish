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

#include "yuv.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {
namespace yuv {

android_ycbcr NV21init(const size_t width, const size_t height, void* data) {
    uint8_t* data8 = static_cast<uint8_t*>(data);
    const size_t area = width * height;

    android_ycbcr nv21;
    nv21.y = data8;
    nv21.cb = data8 + area;
    nv21.cr = data8 + area + (area >> 2);
    nv21.ystride = width;
    nv21.cstride = width / 2;
    nv21.chroma_step = 1;

    return nv21;
}
}  // namespace yuv
}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
