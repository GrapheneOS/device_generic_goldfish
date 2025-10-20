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
#include <memory>

#include <aidl/android/hardware/radio/ims/BnRadioIms.h>
#include "AtChannel.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
using ::ndk::ScopedAStatus;

struct RadioIms : public ims::BnRadioIms {
    RadioIms(std::shared_ptr<AtChannel> atChannel);

    ScopedAStatus setSrvccCallInfo(
            int32_t serial, const std::vector<ims::SrvccCall>& srvccCalls) override;
    ScopedAStatus updateImsRegistrationInfo(
            int32_t serial, const ims::ImsRegistration& imsRegistration) override;
    ScopedAStatus startImsTraffic(
            int32_t serial, int32_t token, ims::ImsTrafficType imsTrafficType,
            AccessNetwork accessNetworkType, ims::ImsCall::Direction trafficDirection) override;
    ScopedAStatus stopImsTraffic(int32_t serial, int32_t token) override;
    ScopedAStatus triggerEpsFallback(
            int32_t serial, ims::EpsFallbackReason reason) override;
    ScopedAStatus sendAnbrQuery(
            int32_t serial, ims::ImsStreamType mediaType,
            ims::ImsStreamDirection direction, int32_t bitsPerSecond) override;
    ScopedAStatus updateImsCallStatus(
            int32_t serial, const std::vector<ims::ImsCall>& imsCalls) override;
    ScopedAStatus updateAllowedServices(int32_t serial,
                                        const std::vector<ims::ImsService>& imsServices) override;

    void atResponseSink(const AtResponsePtr& response);

    ScopedAStatus setResponseFunctions(
            const std::shared_ptr<ims::IRadioImsResponse>& radioImsResponse,
            const std::shared_ptr<ims::IRadioImsIndication>& radioImsIndication) override;

    std::shared_ptr<ims::IRadioImsResponse> mRadioImsResponse;
    std::shared_ptr<ims::IRadioImsIndication> mRadioImsIndication;
};

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
