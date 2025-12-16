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
#include <functional>
#include <future>
#include <memory>
#include <mutex>

#include <aidl/android/hardware/radio/config/BnRadioConfig.h>
#include "AtChannel.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
using ::ndk::ScopedAStatus;

struct RadioConfig : public config::BnRadioConfig {
    RadioConfig(std::shared_ptr<AtChannel> atChannel);

    ScopedAStatus getHalDeviceCapabilities(int32_t serial) override;
    ScopedAStatus getNumOfLiveModems(int32_t serial) override;
    ScopedAStatus getPhoneCapability(int32_t serial) override;
    ScopedAStatus getSimultaneousCallingSupport(int32_t serial) override;
    ScopedAStatus getSimSlotsStatus(int32_t serial) override;
    ScopedAStatus setNumOfLiveModems(int32_t serial, int8_t numOfLiveModems) override;
    ScopedAStatus setPreferredDataModem(int32_t serial, int8_t modemId) override;
    ScopedAStatus setSimSlotsMapping(
            int32_t serial, const std::vector<config::SlotPortMapping>& slotMap) override;
    ScopedAStatus getSimTypeInfo(int32_t serial) override;
    ScopedAStatus setSimType(int32_t serial, const std::vector<config::SimType>& simTypes) override;

    void atResponseSink(const AtResponsePtr& response);
    template <class IGNORE> void handleUnsolicited(const IGNORE&) {}

    ScopedAStatus setResponseFunctions(
            const std::shared_ptr<config::IRadioConfigResponse>& radioConfigResponse,
            const std::shared_ptr<config::IRadioConfigIndication>& radioConfigIndication) override;

private:
    const std::shared_ptr<AtChannel> mAtChannel;
    AtChannel::Conversation mAtConversation;
    std::shared_ptr<config::IRadioConfigResponse> mRadioConfigResponse;
    std::shared_ptr<config::IRadioConfigIndication> mRadioConfigIndication;

};

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
