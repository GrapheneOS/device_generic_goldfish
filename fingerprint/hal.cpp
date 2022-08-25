/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "hal.h"
#include "session.h"
#include "storage.h"

namespace aidl::android::hardware::biometrics::fingerprint {
namespace {
constexpr char HW_COMPONENT_ID[] = "FingerprintSensor";
constexpr char XW_VERSION[] = "ranchu/fingerprint/aidl";
constexpr char FW_VERSION[] = "1";
constexpr char SERIAL_NUMBER[] = "00000001";
constexpr char SW_COMPONENT_ID[] = "matchingAlgorithm";
} // namespace

ndk::ScopedAStatus Hal::getSensorProps(std::vector<SensorProps>* out) {
    const std::vector<common::ComponentInfo> componentInfo = {
        {
            HW_COMPONENT_ID,
            XW_VERSION,
            FW_VERSION,
            SERIAL_NUMBER,
            "" /* softwareVersion */
        },
        {
            SW_COMPONENT_ID,
            "" /* hardwareVersion */,
            "" /* firmwareVersion */,
            "" /* serialNumber */,
            XW_VERSION
        }
    };

    const SensorLocation sensorLocation = {
        0 /* displayId */,
        0 /* sensorLocationX */,
        0 /* sensorLocationY */,
        0 /* sensorRadius */
    };

    *out = {
        {
            {
                0,                          // sensorId
                common::SensorStrength::STRONG,
                Storage::getMaxEnrollmentsPerUser(),
                componentInfo
            },
            FingerprintSensorType::REAR,    // sensorType
            {
                sensorLocation
            },
            false,                          // supportsNavigationGestures
            true,                           // supportsDetectInteraction
        }
    };

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Hal::createSession(const int32_t sensorId,
                                      const int32_t userId,
                                      const std::shared_ptr<ISessionCallback>& cb,
                                      std::shared_ptr<ISession>* out) {
    *out = SharedRefBase::make<Session>(sensorId, userId, cb);
    return ndk::ScopedAStatus::ok();
}

} // namespace aidl::android::hardware::biometrics::fingerprint
