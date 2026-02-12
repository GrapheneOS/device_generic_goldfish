/*
 * Copyright 2022 The Android Open Source Project
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

#include <cstdint>

#include "ihevc_typedefs.h"
#include "ihevcd_cxa.h"
#include "GoldfishDecHelper.h"

namespace android {

struct HevcTraits {
    using DecodeIp = ihevcd_cxa_video_decode_ip_t;
    using DecodeOp = ihevcd_cxa_video_decode_op_t;

    static void createDecoder(iv_obj_t *&decHandle, IV_COLOR_FORMAT_T colorFormat, int numCores, int &stride);
    static void destroyDecoder(iv_obj_t *decHandle);
    static void setParams(iv_obj_t *decHandle, size_t stride, IVD_VIDEO_DECODE_MODE_T dec_mode);
    static void resetDecoder(iv_obj_t *decHandle);
    static void setNumCores(iv_obj_t *decHandle, int numCores);
    static IV_API_CALL_STATUS_T callApi(iv_obj_t *decHandle, void *ip, void *op);
    static bool isKeyFrame(const uint8_t *frame, int inSize);
    static bool shouldIgnoreApiError() { return false; }
};

using GoldfishHevcHelper = GoldfishDecHelper<HevcTraits>;

} // namespace android
