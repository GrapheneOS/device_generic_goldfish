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

#define FAILURE_DEBUG_PREFIX "RadioConfig"

#include <aidl/android/hardware/radio/sim/CardStatus.h>

#include "RadioConfig.h"

#include "atCmds.h"
#include "debug.h"
#include "makeRadioResponseInfo.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
constexpr int8_t kLogicalModemId = 0;

RadioConfig::RadioConfig(std::shared_ptr<AtChannel> atChannel) : mAtChannel(std::move(atChannel)) {
}

ScopedAStatus RadioConfig::getHalDeviceCapabilities(const int32_t serial) {
    NOT_NULL(mRadioConfigResponse)->getHalDeviceCapabilitiesResponse(
            makeRadioResponseInfo(serial), false);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::getNumOfLiveModems(const int32_t serial) {
    NOT_NULL(mRadioConfigResponse)->getNumOfLiveModemsResponse(
            makeRadioResponseInfo(serial), 1);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::getPhoneCapability(const int32_t serial) {
    config::PhoneCapability capability = {
        .maxActiveData = 1,
        .maxActiveInternetData = 1,
        .isInternetLingeringSupported = false,
        .logicalModemIds = { kLogicalModemId },
        .maxActiveVoice = 1,
    };
    NOT_NULL(mRadioConfigResponse)->getPhoneCapabilityResponse(
            makeRadioResponseInfo(serial), std::move(capability));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::getSimultaneousCallingSupport(const int32_t serial) {
    NOT_NULL(mRadioConfigResponse)->getSimultaneousCallingSupportResponse(
            makeRadioResponseInfoNOP(serial), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::getSimSlotsStatus(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using config::SimSlotStatus;
        using config::SimPortInfo;
        using CmeError = AtResponse::CmeError;
        using CPIN = AtResponse::CPIN;

        SimSlotStatus simSlotStatus;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getSimCardStatus,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CPIN>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
failed:     NOT_NULL(mRadioConfigResponse)->getSimSlotsStatusResponse(
                makeRadioResponseInfo(serial, FAILURE(RadioError::INTERNAL_ERR)), {});
            return false;
        } else if (const CPIN* cpin = response->get_if<CPIN>()) {
            switch (cpin->state) {
            case CPIN::State::READY:
                simSlotStatus.cardState = sim::CardStatus::STATE_PRESENT;
                simSlotStatus.atr = "";  // TODO 3BF000818000
                simSlotStatus.eid = "";  // TODO
                break;

            case CPIN::State::PIN:
            case CPIN::State::PUK:
                simSlotStatus.cardState = sim::CardStatus::STATE_RESTRICTED;
                break;

            default:
                goto failed;
            }
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            switch (cmeError->error) {
            case RadioError::SIM_ABSENT:
                simSlotStatus.cardState = sim::CardStatus::STATE_ABSENT;
                break;
            case RadioError::SIM_BUSY:
            case RadioError::SIM_ERR:
                simSlotStatus.cardState = sim::CardStatus::STATE_ERROR;
                break;

            default:
                RLOGE("%s:%s:%s:%d unexpected error: '%s'",
                      FAILURE_DEBUG_PREFIX, kFunc, "CPIN", __LINE__,
                      toString(cmeError->error).c_str());
                goto failed;
            }
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        if (simSlotStatus.cardState != sim::CardStatus::STATE_ABSENT) {
            SimPortInfo simPortInfo = {
                .logicalSlotId = 0,
                .portActive = true,
            };

            response =
                mAtConversation(requestPipe, atCmds::getICCID,
                                [](const AtResponse& response) -> bool {
                                   return response.holds<std::string>();
                                });
            if (!response || response->isParseError()) {
                goto failed;
            } else if (const std::string* iccid = response->get_if<std::string>()) {
                simPortInfo.iccId = *iccid;
            } else {
                response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
            }

            simSlotStatus.portInfo.push_back(std::move(simPortInfo));
        }

        NOT_NULL(mRadioConfigResponse)->getSimSlotsStatusResponse(
                makeRadioResponseInfo(serial), { std::move(simSlotStatus) });
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::setNumOfLiveModems(const int32_t serial,
                                              const int8_t numOfLiveModems) {
    const RadioError result = (numOfLiveModems == 1) ?
        RadioError::NONE : FAILURE_V(RadioError::INVALID_ARGUMENTS,
                                     "numOfLiveModems=%d", numOfLiveModems);

    NOT_NULL(mRadioConfigResponse)->setNumOfLiveModemsResponse(
            makeRadioResponseInfo(serial, result));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::setPreferredDataModem(const int32_t serial,
                                                 const int8_t modemId) {
    const RadioError result = (modemId == kLogicalModemId) ?
        RadioError::NONE : FAILURE_V(RadioError::INVALID_ARGUMENTS,
                                     "modemId=%d", modemId);

    NOT_NULL(mRadioConfigResponse)->setPreferredDataModemResponse(
        makeRadioResponseInfo(serial, result));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::setSimSlotsMapping(
        const int32_t serial, const std::vector<config::SlotPortMapping>& /*slotMap*/) {
    NOT_NULL(mRadioConfigResponse)->setSimSlotsMappingResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::getSimTypeInfo(int32_t serial) {
    config::SimTypeInfo sti = {
        .currentSimType = config::SimType::PHYSICAL,
        .supportedSimTypes = static_cast<int>(config::SimType::PHYSICAL),
    };

    NOT_NULL(mRadioConfigResponse)->getSimTypeInfoResponse(
        makeRadioResponseInfo(serial), {std::move(sti)});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioConfig::setSimType(int32_t serial, const std::vector<config::SimType>& /*simTypes*/) {
    NOT_NULL(mRadioConfigResponse)->setSimTypeResponse(
        makeRadioResponseInfoUnsupported(
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

void RadioConfig::atResponseSink(const AtResponsePtr& response) {
    if (!mAtConversation.send(response)) {
        response->visit([this](const auto& msg){ handleUnsolicited(msg); });
    }
}

ScopedAStatus RadioConfig::setResponseFunctions(
        const std::shared_ptr<config::IRadioConfigResponse>& radioConfigResponse,
        const std::shared_ptr<config::IRadioConfigIndication>& radioConfigIndication) {
    mRadioConfigResponse = NOT_NULL(radioConfigResponse);
    mRadioConfigIndication = NOT_NULL(radioConfigIndication);
    return ScopedAStatus::ok();
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
