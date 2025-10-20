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

#include <aidl/android/hardware/radio/RadioResponseInfo.h>

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {

RadioResponseInfo makeRadioResponseInfo(int32_t serial);
RadioResponseInfo makeRadioResponseInfo(int32_t serial, RadioError);
RadioResponseInfo makeRadioResponseInfoUnsupported(int32_t serial,
                                                   const char* klass,
                                                   const char* method);

inline RadioResponseInfo makeRadioResponseInfoDeprecated(int32_t serial) {
    return makeRadioResponseInfo(serial, RadioError::REQUEST_NOT_SUPPORTED);
}

// the same as makeRadioResponseInfo, but allows grepping
inline RadioResponseInfo makeRadioResponseInfoNOP(int32_t serial) {
    return makeRadioResponseInfo(serial);
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
