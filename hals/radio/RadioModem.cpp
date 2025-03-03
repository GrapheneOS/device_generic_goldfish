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

#define FAILURE_DEBUG_PREFIX "RadioModem"

#include <charconv>
#include <format>

#include "RadioModem.h"

#include "atCmds.h"
#include "debug.h"
#include "makeRadioResponseInfo.h"
#include "ratUtils.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
namespace {
constexpr char kBasebandversion[] = "1.0.0.0";
constexpr char kModemUuid[] = "com.android.modem.simulator";
constexpr char kSimUuid[] = "com.android.modem.simcard";
}  // namespace

RadioModem::RadioModem(std::shared_ptr<AtChannel> atChannel) : mAtChannel(std::move(atChannel)) {
}

ScopedAStatus RadioModem::enableModem(const int32_t serial, const bool /*on*/) {
    NOT_NULL(mRadioModemResponse)->enableModemResponse(
        makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getBasebandVersion(const int32_t serial) {
    NOT_NULL(mRadioModemResponse)->getBasebandVersionResponse(
            makeRadioResponseInfo(serial), kBasebandversion);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getImei(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using namespace std::literals;

        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getIMEI,
                            [](const AtResponse& response) -> bool {
                                return response.holds<std::string>();
                            });
        if (!response) {
            NOT_NULL(mRadioModemResponse)->getImeiResponse(
                    makeRadioResponseInfo(serial, FAILURE(RadioError::INTERNAL_ERR)), {});
            return false;
        } else if (const std::string* imeiSvn = response->get_if<std::string>()) {
            using modem::ImeiInfo;

            ImeiInfo imeiInfo = {
                .type = ImeiInfo::ImeiType::PRIMARY,
                .imei = imeiSvn->substr(0, 15),
                .svn = imeiSvn->substr(15, 2),
            };

            NOT_NULL(mRadioModemResponse)->getImeiResponse(
                makeRadioResponseInfo(serial), std::move(imeiInfo));
           return true;
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getHardwareConfig(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using modem::HardwareConfig;
        using modem::HardwareConfigModem;
        using modem::HardwareConfigSim;

        std::vector<HardwareConfig> config;

        const auto [status, rafBitmask] =
            getSupportedRadioTechs(requestPipe, mAtConversation);
        if (status == RadioError::NONE) {
            const HardwareConfigModem modemHwConfig = {
                .rilModel = 0,  // 0 - single: one-to-one relationship between a modem hardware and a ril daemon.
                .rat = static_cast<RadioTechnology>(rafBitmask),
                .maxVoiceCalls = 1,
                .maxDataCalls = 1,
                .maxStandby = 1,
            };

            HardwareConfig modemConfig = {
                .type = HardwareConfig::TYPE_MODEM,
                .uuid = kModemUuid,
                .state = HardwareConfig::STATE_ENABLED,
            };

            modemConfig.modem.push_back(modemHwConfig);

            HardwareConfig simConfig = {
                .type = HardwareConfig::TYPE_SIM,
                .uuid = kSimUuid,
                .state = HardwareConfig::STATE_ENABLED,
            };

            HardwareConfigSim simHwConfig = {
                .modemUuid = modemConfig.uuid,
            };
            simConfig.sim.push_back(simHwConfig);

            config.push_back(std::move(modemConfig));
            config.push_back(std::move(simConfig));
        }

        NOT_NULL(mRadioModemResponse)->getHardwareConfigResponse(
                makeRadioResponseInfo(serial, status), std::move(config));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getModemActivityInfo(const int32_t serial) {
    using modem::ActivityStatsInfo;
    using modem::ActivityStatsTechSpecificInfo;

    ActivityStatsInfo activityStatsInfo = {
        .sleepModeTimeMs = 42,
        .idleModeTimeMs = 14,
        .techSpecificInfo = {
            {
                .frequencyRange = ActivityStatsTechSpecificInfo::FREQUENCY_RANGE_UNKNOWN,
                .txmModetimeMs = { 1, 3, 6, 8, 9 },
                .rxModeTimeMs = 9,
            },
        },
    };

    NOT_NULL(mRadioModemResponse)->getModemActivityInfoResponse(
            makeRadioResponseInfo(serial), std::move(activityStatsInfo));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getModemStackStatus(const int32_t serial) {
    NOT_NULL(mRadioModemResponse)->getModemStackStatusResponse(
            makeRadioResponseInfo(serial), true);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::getRadioCapability(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using modem::RadioCapability;
        RadioCapability cap;

        const auto [status, rafBitmask] =
            getSupportedRadioTechs(requestPipe, mAtConversation);
        if (status == RadioError::NONE) {
            cap.session = serial;
            cap.phase = RadioCapability::PHASE_CONFIGURED;
            cap.raf = rafBitmask;
            cap.logicalModemUuid = kModemUuid;
            cap.status = RadioCapability::STATUS_SUCCESS;
        }

        NOT_NULL(mRadioModemResponse)->getRadioCapabilityResponse(
                makeRadioResponseInfo(serial, status), std::move(cap));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::requestShutdown(const int32_t serial) {
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        if (setRadioPowerImpl(requestPipe, false)) {
            NOT_NULL(mRadioModemResponse)->requestShutdownResponse(
                makeRadioResponseInfo(serial));
            return true;
        } else {
            return false;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::sendDeviceState(const int32_t serial,
                                          const modem::DeviceStateType /*stateType*/,
                                          const bool /*state*/) {
    NOT_NULL(mRadioModemResponse)->sendDeviceStateResponse(
            makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::setRadioCapability(const int32_t serial,
                                             const modem::RadioCapability& /*rc*/) {
    NOT_NULL(mRadioModemResponse)->setRadioCapabilityResponse(
        makeRadioResponseInfoUnsupported(  // reference-ril.c returns OK but does nothing
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::setRadioPower(const int32_t serial, const bool powerOn,
                                        const bool forEmergencyCall,
                                        const bool preferredForEmergencyCall) {
    mAtChannel->queueRequester([this, serial, powerOn]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        if (setRadioPowerImpl(requestPipe, powerOn)) {
            NOT_NULL(mRadioModemResponse)->setRadioPowerResponse(
                    makeRadioResponseInfo(serial));
            return true;
        } else {
            return false;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::responseAcknowledgement() {
    return ScopedAStatus::ok();
}

void RadioModem::atResponseSink(const AtResponsePtr& response) {
    if (!mAtConversation.send(response)) {
        response->visit([this](const auto& msg){ handleUnsolicited(msg); });
    }
}

void RadioModem::handleUnsolicited(const AtResponse::CFUN& cfun) {
    bool changed;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        changed = (mRadioState != cfun.state);
        mRadioState = cfun.state;
    }

    if (changed && mRadioModemIndication) {
        mRadioModemIndication->radioStateChanged(
            RadioIndicationType::UNSOLICITED, cfun.state);
    }
}

ScopedAStatus RadioModem::setResponseFunctions(
        const std::shared_ptr<modem::IRadioModemResponse>& radioModemResponse,
        const std::shared_ptr<modem::IRadioModemIndication>& radioModemIndication) {
    mRadioModemResponse = NOT_NULL(radioModemResponse);
    mRadioModemIndication = NOT_NULL(radioModemIndication);

    modem::RadioState radioState;

    {
        std::lock_guard<std::mutex> lock(mMtx);
        radioState = mRadioState;
    }

    radioModemIndication->rilConnected(RadioIndicationType::UNSOLICITED);

    radioModemIndication->radioStateChanged(
            RadioIndicationType::UNSOLICITED, radioState);

    return ScopedAStatus::ok();
}

std::pair<RadioError, uint32_t> RadioModem::getSupportedRadioTechs(
            const AtChannel::RequestPipe requestPipe,
            AtChannel::Conversation& atConversation) {
    using ParseError = AtResponse::ParseError;
    using CTEC = AtResponse::CTEC;
    using ratUtils::ModemTechnology;

    AtResponsePtr response =
        atConversation(requestPipe, atCmds::getSupportedRadioTechs,
                        [](const AtResponse& response) -> bool {
                            return response.holds<CTEC>();
                        });
    if (!response || response->isParseError()) {
        return {FAILURE(RadioError::INTERNAL_ERR), 0};
    } else if (const CTEC* ctec = response->get_if<CTEC>()) {
        uint32_t rafBitmask = 0;

        for (const std::string& mtechStr : ctec->values) {
            int mtech;
            std::from_chars(&*mtechStr.begin(), &*mtechStr.end(), mtech, 10);

            rafBitmask |= ratUtils::supportedRadioTechBitmask(
                static_cast<ModemTechnology>(mtech));
        }

        return {RadioError::NONE, rafBitmask};
    } else {
        response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
    }
}

bool RadioModem::setRadioPowerImpl(const AtChannel::RequestPipe requestPipe,
                                   const bool powerOn) {
    {
        std::lock_guard<std::mutex> lock(mMtx);
        if (powerOn == (mRadioState == modem::RadioState::ON)) {
            return true;
        }
    }

    const std::string request = std::format("AT+CFUN={0:d}", powerOn ? 1 : 0);
    if (!requestPipe(request)) {
        return FAILURE(false);
    }

    // to broadcast CFUN from the listening thread
    if (!requestPipe(atCmds::getModemPowerState)) {
        return FAILURE(false);
    }

    using modem::RadioState;

    const modem::RadioState newState =
        powerOn ? RadioState::ON : RadioState::OFF;

    {
        std::lock_guard<std::mutex> lock(mMtx);
        mRadioState = newState;
    }

    NOT_NULL(mRadioModemIndication)->radioStateChanged(
            RadioIndicationType::UNSOLICITED, newState);

    return true;
}

/************************* deprecated *************************/
ScopedAStatus RadioModem::getDeviceIdentity(const int32_t serial) {
    NOT_NULL(mRadioModemResponse)->getDeviceIdentityResponse(
        makeRadioResponseInfoDeprecated(serial),
        "", "", "", "");
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::nvReadItem(const int32_t serial, modem::NvItem) {
    NOT_NULL(mRadioModemResponse)->nvReadItemResponse(
        makeRadioResponseInfoDeprecated(serial), "");
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::nvResetConfig(const int32_t serial, modem::ResetNvType) {
    NOT_NULL(mRadioModemResponse)->nvResetConfigResponse(
        makeRadioResponseInfoDeprecated(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::nvWriteCdmaPrl(const int32_t serial, const std::vector<uint8_t>&) {
    NOT_NULL(mRadioModemResponse)->nvWriteCdmaPrlResponse(
        makeRadioResponseInfoDeprecated(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioModem::nvWriteItem(const int32_t serial, const modem::NvWriteItem&) {
    NOT_NULL(mRadioModemResponse)->nvWriteItemResponse(
        makeRadioResponseInfoDeprecated(serial));
    return ScopedAStatus::ok();
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
