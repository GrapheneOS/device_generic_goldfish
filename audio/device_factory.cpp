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

#include <system/audio.h>
#include <log/log.h>
#include "device_factory.h"
#include "primary_device.h"
#include "debug.h"

namespace android {
namespace hardware {
namespace audio {
namespace V6_0 {
namespace implementation {

using ::android::hardware::Void;

wp<PrimaryDevice> gPrimaryDevice;   // volume levels and the mic state are global

template <class D> sp<D> getCachedDevice(wp<D>& cache) {
    sp<D> result = cache.promote();
    if (!result) {
        result = new D;
        cache = result;
    }
    return result;
}

Return<void> DevicesFactory::openDevice(const hidl_string& device,
                                        openDevice_cb _hidl_cb) {
    Result result = Result::OK;
    sp<IDevice> dev;

    if (device == AUDIO_HARDWARE_MODULE_ID_PRIMARY) {
        dev = getCachedDevice(gPrimaryDevice);
    } else if (device == AUDIO_HARDWARE_MODULE_ID_REMOTE_SUBMIX) {
        dev = getCachedDevice(gPrimaryDevice);
    } else {
        result = FAILURE(Result::INVALID_ARGUMENTS);
    }

    if (!dev) {
        ALOGE("DevicesFactory::%s:%d: failed, device='%s' result='%s'",
              __func__, __LINE__, device.c_str(), toString(result).c_str());
    }

    _hidl_cb(result, std::move(dev));
    return Void();
}

Return<void> DevicesFactory::openPrimaryDevice(openPrimaryDevice_cb _hidl_cb) {
    _hidl_cb(Result::OK, getCachedDevice(gPrimaryDevice));
    return Void();
}

}  // namespace implementation
}  // namespace V6_0
}  // namespace audio
}  // namespace hardware
}  // namespace android
