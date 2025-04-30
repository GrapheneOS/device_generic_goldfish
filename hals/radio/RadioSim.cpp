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

#define FAILURE_DEBUG_PREFIX "RadioSim"

#include <charconv>
#include <format>
#include <tuple>
#include <vector>

#include "RadioSim.h"

#include "atCmds.h"
#include "debug.h"
#include "hexbin.h"
#include "makeRadioResponseInfo.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
namespace {
using namespace std::literals;

enum class AuthContext {
    SIM = 128,
    AKA = 129,
};

enum class StkCmdType {
    RUN_AT        = 0x34,
    SEND_DTMF     = 0x14,
    SEND_SMS      = 0x13,
    SEND_SS       = 0x11,
    SEND_USSD     = 0x12,
    PLAY_TONE     = 0x20,
    OPEN_CHANNEL  = 0x40,
    CLOSE_CHANNEL = 0x41,
    RECEIVE_DATA  = 0x42,
    SEND_DATA     = 0x43,
    GET_CHANNEL_STATUS = 0x44,
    REFRESH       = 0x01,
};

#define USIM_DATA_OFFSET_2                      2
#define USIM_DATA_OFFSET_3                      3
#define USIM_RESPONSE_DATA_FILE_RECORD_LEN_1         6
#define USIM_RESPONSE_DATA_FILE_RECORD_LEN_2         7
#define USIM_TYPE_FILE_DES_LEN                       5

#define USIM_RESPONSE_DATA_FILE_DES_FLAG             2
#define USIM_RESPONSE_DATA_FILE_DES_LEN_FLAG         3

#define USIM_FILE_DES_TAG                       0x82
#define USIM_FILE_SIZE_TAG                      0x80


#define SIM_RESPONSE_EF_SIZE                        15
#define SIM_RESPONSE_DATA_FILE_SIZE_1               2
#define SIM_RESPONSE_DATA_FILE_SIZE_2               3
#define SIM_RESPONSE_DATA_FILE_TYPE                 6
#define SIM_RESPONSE_DATA_STRUCTURE                 13
#define SIM_RESPONSE_DATA_RECORD_LENGTH             14
#define SIM_TYPE_EF                                 4

enum class UsimEfType {
    TRANSPARENT = 1,
    LINEAR_FIXED = 2,
    CYCLIC = 6,
};

// 62 17 82 02 41 2183022FE28A01058B032F06038002000A880110
bool convertUsimToSim(const std::vector<uint8_t>& bytesUSIM, std::string* hexSIM) {
    const size_t sz = bytesUSIM.size();
    size_t i = 0;

    size_t desIndex;
    while (true) {
        if (bytesUSIM[i] == USIM_FILE_DES_TAG) {
            desIndex = i;
            break;
        } else {
            ++i;
            if (i >= sz) {
                return false;
            }
        }
    }

    size_t sizeIndex;
    while (true) {
        if (bytesUSIM[i] == USIM_FILE_SIZE_TAG) {
            sizeIndex = i;
            break;
        } else {
            i += bytesUSIM[i + 1] + 2;
            if (i >= sz) {
                return FAILURE(false);
            }
        }
    }

    uint8_t bytesSIM[SIM_RESPONSE_EF_SIZE] = {0};
    switch (static_cast<UsimEfType>(bytesUSIM[desIndex + USIM_RESPONSE_DATA_FILE_DES_FLAG] & 0x07)) {
    case UsimEfType::TRANSPARENT:
        bytesSIM[SIM_RESPONSE_DATA_STRUCTURE] = 0;
        break;

    case UsimEfType::LINEAR_FIXED:
        if (USIM_FILE_DES_TAG != bytesUSIM[USIM_RESPONSE_DATA_FILE_DES_FLAG]) {
            return FAILURE(false);
        }
        if (USIM_TYPE_FILE_DES_LEN != bytesUSIM[USIM_RESPONSE_DATA_FILE_DES_LEN_FLAG]) {
            return FAILURE(false);
        }

        bytesSIM[SIM_RESPONSE_DATA_STRUCTURE] = 1;
        bytesSIM[SIM_RESPONSE_DATA_RECORD_LENGTH] =
                //(byteUSIM[USIM_RESPONSE_DATA_FILE_RECORD_LEN_1] << 8) +
                bytesUSIM[USIM_RESPONSE_DATA_FILE_RECORD_LEN_2];
        break;

    case UsimEfType::CYCLIC:
        bytesSIM[SIM_RESPONSE_DATA_STRUCTURE] = 3;
        bytesSIM[SIM_RESPONSE_DATA_RECORD_LENGTH] =
                //(byteUSIM[USIM_RESPONSE_DATA_FILE_RECORD_LEN_1] << 8) +
                bytesUSIM[USIM_RESPONSE_DATA_FILE_RECORD_LEN_2];
        break;

    default:
        return false;
    }

    bytesSIM[SIM_RESPONSE_DATA_FILE_TYPE] = SIM_TYPE_EF;
    bytesSIM[SIM_RESPONSE_DATA_FILE_SIZE_1] =
            bytesUSIM[sizeIndex + USIM_DATA_OFFSET_2];
    bytesSIM[SIM_RESPONSE_DATA_FILE_SIZE_2] =
            bytesUSIM[sizeIndex + USIM_DATA_OFFSET_3];

    *hexSIM = bin2hex(bytesSIM, sizeof(bytesSIM));
    return true;
}

std::optional<int> getRemainingRetries(const std::string_view pinType,
                                       const AtChannel::RequestPipe requestPipe,
                                       AtChannel::Conversation& atConversation) {
    using CPINR = AtResponse::CPINR;

    AtResponsePtr response =
        atConversation(requestPipe, std::format("AT+CPINR=\"{0:s}\"", pinType),
                       [](const AtResponse& response) -> bool {
                          return response.holds<CPINR>();
                       });
    if (!response || response->isParseError()) {
        return FAILURE(std::nullopt);
    } else if (const CPINR* cpinr = response->get_if<CPINR>()) {
        return cpinr->remainingRetryTimes;
    } else {
        response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
    }
}

std::pair<RadioError, int> enterOrChangeSimPinPuk(const bool change,
                                                  const std::string_view oldPin,
                                                  const std::string_view newPin,
                                                  const std::string_view pinType,
                                                  const AtChannel::RequestPipe requestPipe,
                                                  AtChannel::Conversation& atConversation) {
    using CmeError = AtResponse::CmeError;

    std::string request;
    if (change) {
        if (pinType.compare("SIM PIN2"sv) == 0) {
            request = std::format("AT+CPWD=\"{0:s}\",\"{1:s}\",\"{2:s}\"",
                                  "P2"sv, oldPin, newPin);
        } else {
            request = std::format("AT+CPIN={0:s},{1:s}", oldPin, newPin);
        }
    } else {
        request = std::format("AT+CPIN={0:s}", oldPin);
    }

    AtResponsePtr response =
        atConversation(requestPipe, request,
                       [](const AtResponse& response) -> bool {
                          return response.holds<CmeError>() || response.isOK();
                       });
    if (!response || response->isParseError()) {
        return {FAILURE(RadioError::INTERNAL_ERR), 0};
    } else if (response->isOK()) {
        return {RadioError::NONE, 0};
    } else if (!response->get_if<CmeError>()) {
        response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
    }

    const std::optional<int> maybeRetries =
        getRemainingRetries(pinType, requestPipe, atConversation);
    if (maybeRetries) {
        return {RadioError::PASSWORD_INCORRECT, maybeRetries.value()};
    } else {
        return {FAILURE(RadioError::INTERNAL_ERR), 0};
    }
}

// authData64 = base64([randLen][...rand...][authLen][...auth...])
std::tuple<RadioError, std::vector<uint8_t>, std::vector<uint8_t>>
parseAuthData(const AuthContext authContext, const std::string_view authData64) {
    auto maybeAuthData = base64decode(authData64.data(), authData64.size());
    if (!maybeAuthData) {
        return {FAILURE(RadioError::INVALID_ARGUMENTS), {}, {}};
    }

    const std::vector<uint8_t> authData = std::move(maybeAuthData.value());
    const size_t authDataSize = authData.size();
    if (authDataSize == 0) {
        return {FAILURE(RadioError::INVALID_ARGUMENTS), {}, {}};
    }

    const size_t randLen = authData[0];
    if (authDataSize < (1U + randLen)) {
        return {FAILURE(RadioError::INVALID_ARGUMENTS), {}, {}};
    }

    std::vector rand(&authData[1], &authData[1U + randLen]);
    if (authContext == AuthContext::SIM) {
        return {RadioError::NONE, std::move(rand), {}};
    }

    const size_t authLen = authData[1U + randLen];
    if (authDataSize < (1U + randLen + 1U + authLen)) {
        return {FAILURE(RadioError::INVALID_ARGUMENTS), {}, {}};
    }

    std::vector auth(&authData[1U + randLen + 1U],
                     &authData[1U + randLen + 1U + authLen]);
    if (authContext == AuthContext::AKA) {
        return {RadioError::NONE, std::move(rand), std::move(auth)};
    }

    return {FAILURE(RadioError::REQUEST_NOT_SUPPORTED), {}, {}};
}

std::optional<std::vector<uint8_t>> getSelectResponse(const AtChannel::RequestPipe requestPipe,
                                                      AtChannel::Conversation& atConversation,
                                                      const int channel, const int p2) {
    using CGLA = AtResponse::CGLA;
    using CmeError = AtResponse::CmeError;

    const std::string request =
        std::format("AT+CGLA={0:d},14,00A400{1:02X}023F00", channel, p2);
    AtResponsePtr response =
        atConversation(requestPipe, request,
                       [](const AtResponse& response) -> bool {
                          return response.holds<CGLA>() || response.holds<CmeError>();
                       });
    if (!response || response->isParseError()) {
        return FAILURE(std::nullopt);
    } else if (const CGLA* cgla = response->get_if<CGLA>()) {
        if (cgla->response.size() < 4) {
            return FAILURE(std::nullopt);
        }

        int sw12;
        const size_t size4 = cgla->response.size() - 4;
        if (1 != ::sscanf(&cgla->response[size4], "%04x", &sw12)) {
            return FAILURE(std::nullopt);
        }

        if (sw12 != 0x9000) {
            return FAILURE(std::nullopt);
        }

        std::vector<uint8_t> selectResponse;
        if (!hex2bin(cgla->response, &selectResponse)) {
            return FAILURE(std::nullopt);
        }

        return selectResponse;
    } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
        cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, __func__, __LINE__);
        return FAILURE(std::nullopt);
    } else {
        response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
    }
}

}  // namespace

RadioSim::RadioSim(std::shared_ptr<AtChannel> atChannel) : mAtChannel(std::move(atChannel)) {
}

ScopedAStatus RadioSim::areUiccApplicationsEnabled(const int32_t serial) {
    using modem::RadioState;

    RadioState radioState;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        radioState = mRadioState;
    }

    const RadioError status = (radioState == RadioState::ON) ?
        RadioError::NONE : RadioError::RADIO_NOT_AVAILABLE;

    NOT_NULL(mRadioSimResponse)->areUiccApplicationsEnabledResponse(
            makeRadioResponseInfo(serial, status), mUiccApplicationsEnabled);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::changeIccPin2ForApp(int32_t serial,
                                            const std::string& oldPin2,
                                            const std::string& newPin2,
                                            const std::string& /*aid*/) {
    mAtChannel->queueRequester([this, serial, oldPin2, newPin2]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        const auto [status, remainingRetries] =
            enterOrChangeSimPinPuk(true, oldPin2, newPin2, "SIM PIN2"sv,
                                   requestPipe, mAtConversation);

        NOT_NULL(mRadioSimResponse)->supplyIccPin2ForAppResponse(
                makeRadioResponseInfo(serial, status), remainingRetries);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::changeIccPinForApp(const int32_t serial,
                                           const std::string& oldPin,
                                           const std::string& newPin,
                                           const std::string& /*aid*/) {
    mAtChannel->queueRequester([this, serial, oldPin, newPin]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        const auto [status, remainingRetries] =
            enterOrChangeSimPinPuk(true, oldPin, newPin, "SIM PIN"sv,
                                   requestPipe, mAtConversation);

        NOT_NULL(mRadioSimResponse)->changeIccPinForAppResponse(
                makeRadioResponseInfo(serial, status), remainingRetries);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::enableUiccApplications(const int32_t serial, const bool enable) {
    bool changed;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        changed = mUiccApplicationsEnabled != enable;
        mUiccApplicationsEnabled = enable;
    }

    NOT_NULL(mRadioSimResponse)->enableUiccApplicationsResponse(
            makeRadioResponseInfo(serial));

    if (changed && mRadioSimIndication) {
        mRadioSimIndication->uiccApplicationsEnablementChanged(
                RadioIndicationType::UNSOLICITED, enable);
    }
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::getAllowedCarriers(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using sim::CarrierInfo;
        using sim::CarrierRestrictions;
        using sim::SimLockMultiSimPolicy;
        using CmeError = AtResponse::CmeError;
        using COPS = AtResponse::COPS;

        RadioError status = RadioError::NONE;
        CarrierRestrictions carrierRestrictions = {
            .allowedCarriersPrioritized = true,
        };

        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getOperator,
                            [](const AtResponse& response) -> bool {
                                return response.holds<COPS>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const COPS* cops = response->get_if<COPS>()) {
            if ((cops->operators.size() == 1) && (cops->operators[0].isCurrent())) {
                const COPS::OperatorInfo& current = cops->operators[0];
                CarrierInfo ci = {
                    .mcc = current.mcc(),
                    .mnc = current.mnc(),
                };

                carrierRestrictions.allowedCarrierInfoList.push_back(std::move(ci));
            } else {
                response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
            }
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
        }

        NOT_NULL(mRadioSimResponse)->getAllowedCarriersResponse(
            makeRadioResponseInfo(serial, status),
            std::move(carrierRestrictions),
            SimLockMultiSimPolicy::NO_MULTISIM_POLICY);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::getCdmaSubscription(const int32_t serial) {
    NOT_NULL(mRadioSimResponse)->getCdmaSubscriptionResponse(
        makeRadioResponseInfo(serial),
        "8587777777",   // mdn
        "1",            // sid
        "1",            // nid
        "8587777777",   // min
        "1");           // prl
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::getCdmaSubscriptionSource(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CCSS = AtResponse::CCSS;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getCdmaSubscriptionSource,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CCSS>();
                            });
        if (!response || response->isParseError()) {
            NOT_NULL(mRadioSimResponse)->getCdmaSubscriptionSourceResponse(
                    makeRadioResponseInfo(serial, RadioError::INTERNAL_ERR), {});
            return false;
        } else if (const CCSS* csss = response->get_if<CCSS>()) {
            NOT_NULL(mRadioSimResponse)->getCdmaSubscriptionSourceResponse(
                    makeRadioResponseInfo(serial), csss->source);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::getFacilityLockForApp(const int32_t serial, const std::string& facility,
                                              const std::string& password, const int32_t serviceClass,
                                              const std::string& /*appId*/) {
    std::string request = std::format("AT+CLCK=\"{0:s}\",{1:d},\"{2:s}\",{3:d}",
                                      facility, atCmds::kClckQuery, password, serviceClass);

    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, request = std::move(request)]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;
        using CLCK = AtResponse::CLCK;

        RadioError status = RadioError::NONE;
        int lockBitmask = 0;

        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CLCK>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CLCK* clck = response->get_if<CLCK>()) {
            lockBitmask = clck->locked ? 7 : 0;
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioSimResponse)->getFacilityLockForAppResponse(
                makeRadioResponseInfo(serial, status), lockBitmask);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::getIccCardStatus(const int32_t serial) {
    using sim::AppStatus;
    using sim::PersoSubstate;
    using sim::PinState;

    struct AppStatus3 {
        AppStatus usim;
        AppStatus ruim;
        AppStatus isim;
    };

    static const std::string kAidPtr = ""; //"A0000000871002FF86FF0389FFFFFFFF";
    static const std::string kAppLabelPtr = "";

    static const std::string kATR = ""; //"3BF000818000";
    // This data is mandatory and applicable only when cardState is
    // STATE_PRESENT and SIM card supports eUICC.
    static const std::string kEID = "";

    static const AppStatus3 kIccStatusReady = {
        .usim = {
            AppStatus::APP_TYPE_USIM, AppStatus::APP_STATE_READY, PersoSubstate::READY,
            kAidPtr, kAppLabelPtr, false, PinState::UNKNOWN, PinState::UNKNOWN
        },
        .ruim = {
            AppStatus::APP_TYPE_RUIM, AppStatus::APP_STATE_READY, PersoSubstate::READY,
            kAidPtr, kAppLabelPtr, false, PinState::UNKNOWN, PinState::UNKNOWN
        },
        .isim = {
            AppStatus::APP_TYPE_ISIM, AppStatus::APP_STATE_READY, PersoSubstate::READY,
            kAidPtr, kAppLabelPtr, false, PinState::UNKNOWN, PinState::UNKNOWN
        }
    };

    static const AppStatus3 kIccStatusPIN = {
        .usim = {
            AppStatus::APP_TYPE_USIM, AppStatus::APP_STATE_PIN, PersoSubstate::UNKNOWN,
            kAidPtr, kAppLabelPtr, false, PinState::ENABLED_NOT_VERIFIED, PinState::ENABLED_NOT_VERIFIED
        },
        .ruim = {
            AppStatus::APP_TYPE_RUIM, AppStatus::APP_STATE_PIN, PersoSubstate::UNKNOWN,
            kAidPtr, kAppLabelPtr, false, PinState::ENABLED_NOT_VERIFIED, PinState::ENABLED_NOT_VERIFIED
        },
        .isim = {
            AppStatus::APP_TYPE_ISIM, AppStatus::APP_STATE_PIN, PersoSubstate::UNKNOWN,
            kAidPtr, kAppLabelPtr, false, PinState::ENABLED_NOT_VERIFIED, PinState::ENABLED_NOT_VERIFIED
        }
    };

    static const AppStatus3 kIccStatusPUK = {
        .usim = {
            AppStatus::APP_TYPE_USIM, AppStatus::APP_STATE_PUK, PersoSubstate::UNKNOWN,
            kAidPtr, kAppLabelPtr, false, PinState::ENABLED_NOT_VERIFIED, PinState::ENABLED_NOT_VERIFIED
        },
        .ruim = {
            AppStatus::APP_TYPE_RUIM, AppStatus::APP_STATE_PUK, PersoSubstate::UNKNOWN,
            kAidPtr, kAppLabelPtr, false, PinState::ENABLED_NOT_VERIFIED, PinState::ENABLED_NOT_VERIFIED
        },
        .isim = {
            AppStatus::APP_TYPE_ISIM, AppStatus::APP_STATE_PUK, PersoSubstate::UNKNOWN,
            kAidPtr, kAppLabelPtr, false, PinState::ENABLED_NOT_VERIFIED, PinState::ENABLED_NOT_VERIFIED
        }
    };

    static const AppStatus3 kIccStatusBUSY = {
        .usim = {
            AppStatus::APP_TYPE_USIM, AppStatus::APP_STATE_DETECTED, PersoSubstate::UNKNOWN,
            kAidPtr, kAppLabelPtr, false, PinState::UNKNOWN, PinState::UNKNOWN
        },
        .ruim = {
            AppStatus::APP_TYPE_RUIM, AppStatus::APP_STATE_DETECTED, PersoSubstate::UNKNOWN,
            kAidPtr, kAppLabelPtr, false, PinState::UNKNOWN, PinState::UNKNOWN
        },
        .isim = {
            AppStatus::APP_TYPE_ISIM, AppStatus::APP_STATE_DETECTED, PersoSubstate::UNKNOWN,
            kAidPtr, kAppLabelPtr, false, PinState::UNKNOWN, PinState::UNKNOWN
        }
    };

    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using sim::CardStatus;
        using CmeError = AtResponse::CmeError;
        using CPIN = AtResponse::CPIN;

        RadioError status = RadioError::NONE;
        CardStatus cardStatus = {
            .slotMap = {
                .physicalSlotId = -1,  // see ril_service.cpp in CF
                .portId = 0,
            }
        };

        const AppStatus3* appStatus = nullptr;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getSimCardStatus,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CPIN>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
            goto failed;
        } else if (const CPIN* cpin = response->get_if<CPIN>()) {
            switch (cpin->state) {
            case CPIN::State::READY:
                cardStatus.cardState = sim::CardStatus::STATE_PRESENT;
                cardStatus.universalPinState = sim::PinState::UNKNOWN;
                appStatus = &kIccStatusReady;
                break;

            case CPIN::State::PIN:
                cardStatus.cardState = sim::CardStatus::STATE_RESTRICTED;
                cardStatus.universalPinState = sim::PinState::ENABLED_NOT_VERIFIED;
                appStatus = &kIccStatusPIN;
                break;

            case CPIN::State::PUK:
                cardStatus.cardState = sim::CardStatus::STATE_RESTRICTED;
                cardStatus.universalPinState = sim::PinState::ENABLED_NOT_VERIFIED;
                appStatus = &kIccStatusPUK;
                break;

            default:
                status = FAILURE(RadioError::INTERNAL_ERR);
                goto failed;
            }
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            switch (cmeError->error) {
            case RadioError::SIM_ABSENT:
                cardStatus.cardState = sim::CardStatus::STATE_ABSENT;
                cardStatus.universalPinState = sim::PinState::UNKNOWN;
                break;

            case RadioError::SIM_BUSY:
            case RadioError::SIM_ERR:
                cardStatus.cardState = sim::CardStatus::STATE_ERROR;
                cardStatus.universalPinState = sim::PinState::UNKNOWN;
                appStatus = &kIccStatusBUSY;
                break;

            default:
                status = cmeError->getErrorAndLog(
                    FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
                goto failed;
            }
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        if (cardStatus.cardState != sim::CardStatus::STATE_ABSENT) {
            response =
                mAtConversation(requestPipe, atCmds::getICCID,
                                [](const AtResponse& response) -> bool {
                                   return response.holds<std::string>();
                                });
            if (!response || response->isParseError()) {
                status = FAILURE(RadioError::INTERNAL_ERR);
                goto failed;
            } else if (const std::string* iccid = response->get_if<std::string>()) {
                cardStatus.iccid = *iccid;
            } else {
                response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
            }

            cardStatus.applications.push_back(appStatus->usim);
            cardStatus.applications.push_back(appStatus->ruim);
            cardStatus.applications.push_back(appStatus->isim);
            cardStatus.gsmUmtsSubscriptionAppIndex = 0; // usim
            cardStatus.cdmaSubscriptionAppIndex = 1;    // ruim
            cardStatus.imsSubscriptionAppIndex = 2;     // isim

            cardStatus.atr = kATR;
            cardStatus.eid = kEID;
        }

        if (status == RadioError::NONE) {
            NOT_NULL(mRadioSimResponse)->getIccCardStatusResponse(
                    makeRadioResponseInfo(serial), std::move(cardStatus));
            return true;
        } else {
failed:     NOT_NULL(mRadioSimResponse)->getIccCardStatusResponse(
                    makeRadioResponseInfo(serial, status), {});
            return status != RadioError::INTERNAL_ERR;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::getImsiForApp(const int32_t serial, const std::string& /*aid*/) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;
        std::string imsi;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getIMSI,
                            [](const AtResponse& response) -> bool {
                               return response.holds<std::string>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const std::string* pImsi = response->get_if<std::string>()) {
            imsi = *pImsi;
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        if (status == RadioError::NONE) {
            NOT_NULL(mRadioSimResponse)->getImsiForAppResponse(
                    makeRadioResponseInfo(serial), std::move(imsi));
            return true;
        } else {
            NOT_NULL(mRadioSimResponse)->getImsiForAppResponse(
                    makeRadioResponseInfo(serial, FAILURE(status)), {});
            return status != RadioError::INTERNAL_ERR;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::getSimPhonebookCapacity(const int32_t serial) {
    NOT_NULL(mRadioSimResponse)->getSimPhonebookCapacityResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::getSimPhonebookRecords(const int32_t serial) {
    NOT_NULL(mRadioSimResponse)->getSimPhonebookRecordsResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::iccCloseLogicalChannelWithSessionInfo(const int32_t serial,
                                                              const sim::SessionInfo& recordInfo) {
    const int32_t sessionId = recordInfo.sessionId;

    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, sessionId](const AtChannel::RequestPipe requestPipe) -> bool {
        using CCHC = AtResponse::CCHC;
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;

        const std::string request = std::format("AT+CCHC={0:d}", sessionId);
        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CCHC>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
            if (status == RadioError::NO_SUCH_ELEMENT) {
                status = RadioError::INVALID_ARGUMENTS;
            }
        } else if (!response->get_if<CCHC>()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioSimResponse)->iccCloseLogicalChannelWithSessionInfoResponse(
                makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::iccIoForApp(const int32_t serial, const sim::IccIo& iccIo) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, iccIo]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CRSM = AtResponse::CRSM;
        using CmeError = AtResponse::CmeError;
        using sim::IccIoResult;

        RadioError status = RadioError::NONE;
        IccIoResult iccIoResult;

        std::string request;
        if (iccIo.data.empty()) {
            request = std::format("AT+CRSM={0:d},{1:d},{2:d},{3:d},{4:d}",
                    iccIo.command, iccIo.fileId, iccIo.p1, iccIo.p2, iccIo.p3);
        } else {
            request = std::format("AT+CRSM={0:d},{1:d},{2:d},{3:d},{4:d},{5:s},{6:s}",
                    iccIo.command, iccIo.fileId, iccIo.p1, iccIo.p2, iccIo.p3,
                    iccIo.data, iccIo.aid);
        }

        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CRSM>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CRSM* crsm = response->get_if<CRSM>()) {
            iccIoResult.sw1 = crsm->sw1;
            iccIoResult.sw2 = crsm->sw2;

            if (iccIo.command == 192) {  // get
                std::vector<uint8_t> bytes;
                if (hex2bin(crsm->response, &bytes) && !bytes.empty() && (bytes.front() == 0x62)) {
                    if (!convertUsimToSim(bytes, &iccIoResult.simResponse)) {
                        status = FAILURE(RadioError::GENERIC_FAILURE);
                    }
                } else {
                    iccIoResult.simResponse = crsm->response;
                }
            } else {
                iccIoResult.simResponse = crsm->response;
            }
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        if (status == RadioError::NONE) {
            NOT_NULL(mRadioSimResponse)->iccIoForAppResponse(
                    makeRadioResponseInfo(serial), std::move(iccIoResult));
            return true;
        } else {
            NOT_NULL(mRadioSimResponse)->iccIoForAppResponse(
                    makeRadioResponseInfo(serial, status), {});
            return status != RadioError::INTERNAL_ERR;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::iccOpenLogicalChannel(const int32_t serial,
                                              const std::string& aid,
                                              const int32_t p2) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, aid, p2](const AtChannel::RequestPipe requestPipe) -> bool {
        using CSIM = AtResponse::CSIM;
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;
        int channelId = 0;
        std::vector<uint8_t> selectResponse;

        if (aid.empty()) {
            AtResponsePtr response =
                mAtConversation(requestPipe, "AT+CSIM=10,\"0070000001\""sv,
                                [](const AtResponse& response) -> bool {
                                   return response.holds<CSIM>() || response.holds<CmeError>();
                                });
            if (!response || response->isParseError()) {
                status = FAILURE(RadioError::INTERNAL_ERR);
            } else if (const CSIM* csim = response->get_if<CSIM>()) {
                if (1 == ::sscanf(csim->response.c_str(), "%02x", &channelId)) {
                    if (p2 >= 0) {
                        auto maybeSelectResponse =
                            getSelectResponse(requestPipe, mAtConversation,
                                              channelId, p2);
                        if (maybeSelectResponse) {
                            selectResponse = std::move(maybeSelectResponse.value());
                        } else {
                            requestPipe(std::format("AT+CCHC={0:d}", channelId));
                            status = FAILURE(RadioError::GENERIC_FAILURE);
                        }
                    } else {
                        if (!hex2bin(std::string_view(csim->response).substr(2),
                                     &selectResponse)) {
                            status = FAILURE(RadioError::GENERIC_FAILURE);
                        }
                    }
                } else {
                    status = FAILURE(RadioError::GENERIC_FAILURE);
                }
            } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
                status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
            } else {
                response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
            }
        } else {
            const std::string request = std::format("AT+CCHO={0:s}", aid);
            AtResponsePtr response =
                mAtConversation(requestPipe, request,
                                [](const AtResponse& response) -> bool {
                                   return response.holds<std::string>() || response.holds<CmeError>();
                                });
            if (!response || response->isParseError()) {
                status = FAILURE(RadioError::INTERNAL_ERR);
            } else if (const std::string* idStr = response->get_if<std::string>()) {
                const char* end = idStr->data() + idStr->size();

                if (std::from_chars(idStr->data(), end, channelId, 10).ptr != end) {
                    status = FAILURE(RadioError::INTERNAL_ERR);
                }
            } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
                status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
            } else {
                response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
            }
        }

        NOT_NULL(mRadioSimResponse)->iccOpenLogicalChannelResponse(
                makeRadioResponseInfo(serial, status), channelId, std::move(selectResponse));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::iccTransmitApduBasicChannel(const int32_t serial,
                                                    const sim::SimApdu& message) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, message]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CSIM = AtResponse::CSIM;
        using CmeError = AtResponse::CmeError;
        using sim::IccIoResult;

        RadioError status = RadioError::NONE;
        IccIoResult iccIoResult;

        std::string request;
        if (message.data.empty()) {
            if (message.p3 < 0) {
                request = std::format(
                        "AT+CSIM={0:d},\"{1:02x}{2:02x}{3:02x}{4:02x}\"", 8,
                        message.cla, message.instruction, message.p1, message.p2);
            } else {
                request = std::format(
                        "AT+CSIM={0:d},\"{1:02x}{2:02x}{3:02x}{4:02x}{5:02x}\"", 10,
                        message.cla, message.instruction, message.p1, message.p2, message.p3);
            }
        } else {
            const size_t dataSize = 10 + message.data.size();
            request = std::format(
                    "AT+CSIM={0:d},\"{1:02x}{2:02x}{3:02x}{4:02x}{5:02x}{6:s}\"",
                    dataSize, message.cla, message.instruction, message.p1,
                    message.p2, message.p3, message.data);
        }

        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CSIM>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CSIM* csim = response->get_if<CSIM>()) {
            const std::string& simResponse = csim->response;
            if (simResponse.size() >= 4) {
                if (2 == ::sscanf(&simResponse[simResponse.size() - 4], "%02X%02X",
                                  &iccIoResult.sw1, &iccIoResult.sw2)) {
                    iccIoResult.simResponse = simResponse.substr(0, simResponse.size() - 4);
                } else {
                    status = FAILURE(RadioError::GENERIC_FAILURE);
                }
            } else {
                status = FAILURE(RadioError::GENERIC_FAILURE);
            }
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        if (status == RadioError::NONE) {
            NOT_NULL(mRadioSimResponse)->iccTransmitApduBasicChannelResponse(
                makeRadioResponseInfo(serial), std::move(iccIoResult));
            return true;
        } else {
            NOT_NULL(mRadioSimResponse)->iccTransmitApduBasicChannelResponse(
                makeRadioResponseInfo(serial, status), {});
            return status != RadioError::INTERNAL_ERR;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::iccTransmitApduLogicalChannel(
        const int32_t serial, const sim::SimApdu& message) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, message]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CGLA = AtResponse::CGLA;
        using CmeError = AtResponse::CmeError;
        using sim::IccIoResult;

        RadioError status = RadioError::NONE;
        IccIoResult iccIoResult;

        const size_t dataSize = 10 + message.data.size();
        const std::string request = std::format(
                "AT+CGLA={0:d},{1:d},{2:02x}{3:02x}{4:02x}{5:02x}{6:02x}{7:s}",
                message.sessionId, dataSize,
                message.cla, message.instruction, message.p1,
                message.p2, message.p3, message.data);
        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CGLA>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CGLA* cgla = response->get_if<CGLA>()) {
            if (cgla->response.size() >= 4) {
                const size_t size4 = cgla->response.size() - 4;
                if (2 == ::sscanf(&cgla->response[size4], "%02x%02x",
                                  &iccIoResult.sw1, &iccIoResult.sw2)) {
                    iccIoResult.simResponse = cgla->response.substr(0, size4);
                } else {
                    status = FAILURE(RadioError::GENERIC_FAILURE);
                }
            } else {
                status = FAILURE(RadioError::GENERIC_FAILURE);
            }
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        if (status == RadioError::NONE) {
            NOT_NULL(mRadioSimResponse)->iccTransmitApduLogicalChannelResponse(
                makeRadioResponseInfo(serial), std::move(iccIoResult));
            return true;
        } else {
            NOT_NULL(mRadioSimResponse)->iccTransmitApduLogicalChannelResponse(
                makeRadioResponseInfo(serial, status), {});
            return status != RadioError::INTERNAL_ERR;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::reportStkServiceIsRunning(const int32_t serial) {
    decltype(mStkUnsolResponse) stkUnsolResponse;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        mStkServiceRunning = true;
        stkUnsolResponse = std::move(mStkUnsolResponse);
    }

    if (stkUnsolResponse) {
        NOT_NULL(mRadioSimIndication)->stkProactiveCommand(
            RadioIndicationType::UNSOLICITED, std::move(stkUnsolResponse.value().cmd));
    }

    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CUSATD = AtResponse::CUSATD;

        RadioError status = RadioError::NONE;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::reportStkServiceRunning,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CUSATD>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (!response->get_if<CUSATD>()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioSimResponse)->reportStkServiceIsRunningResponse(
                makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::requestIccSimAuthentication(const int32_t serial,
                                                    const int32_t authContextInt,
                                                    const std::string& authData64,
                                                    const std::string& /*aid*/) {
    const AuthContext authContext = static_cast<AuthContext>(authContextInt);

    auto [status, randBin, authBin] = parseAuthData(authContext, authData64);
    if (status != RadioError::NONE) {
        NOT_NULL(mRadioSimResponse)->requestIccSimAuthenticationResponse(
                makeRadioResponseInfo(serial, status), {});
        return ScopedAStatus::ok();
    }

    std::string randHex = bin2hex(randBin.data(), randBin.size());
    std::string authHex = bin2hex(authBin.data(), authBin.size());

    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, authContext,
                                randHex = std::move(randHex),
                                authHex = std::move(authHex)]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;
        using MBAU = AtResponse::MBAU;
        using sim::IccIoResult;

        RadioError status = RadioError::NONE;
        IccIoResult iccIoResult;

        std::string request;
        switch (authContext) {
        case AuthContext::SIM:
            request = std::format("AT^MBAU=\"{0:s}\"", randHex);
            break;

        case AuthContext::AKA:
            request = std::format("AT^MBAU=\"{0:s},{1:s}\"", randHex, authHex);  // the quotes are interesting here
            break;

        default:
            return FAILURE(false);
        }

        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<MBAU>() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const MBAU* mbau = response->get_if<MBAU>()) {
            const auto putByte = [](uint8_t* dst, uint8_t b) -> uint8_t* {
                *dst = b;
                return dst + 1;
            };

            const auto putRange = [](uint8_t* dst, const uint8_t* src, size_t size) -> uint8_t* {
                memcpy(dst, src, size);
                return dst + size;
            };

            const auto putSizedRange = [putByte, putRange](uint8_t* dst, const uint8_t* src, size_t size) -> uint8_t* {
                return putRange(putByte(dst, size), src, size);
            };

            std::vector<uint8_t> responseBin;
            uint8_t* p;

            switch (authContext) {
            case AuthContext::SIM:  // sresLen + sres + kcLen + kc
                responseBin.resize(2 + mbau->sres.size() + mbau->kc.size());
                p = responseBin.data();
                p = putSizedRange(p, mbau->sres.data(), mbau->sres.size());
                p = putSizedRange(p, mbau->kc.data(), mbau->kc.size());
                break;

            case AuthContext::AKA:  // 0xDB + ckLen + ck + ikLen + ik + resAutsLen + resAuts
                responseBin.resize(4 + mbau->ck.size() + mbau->ik.size() + mbau->resAuts.size());
                p = responseBin.data();
                p = putByte(p, 0xDB);
                p = putSizedRange(p, mbau->ck.data(), mbau->ck.size());
                p = putSizedRange(p, mbau->ik.data(), mbau->ik.size());
                p = putSizedRange(p, mbau->resAuts.data(), mbau->resAuts.size());
                break;
            }

            iccIoResult.sw1 = 0x90;
            iccIoResult.sw2 = 0;
            iccIoResult.simResponse = base64encode(responseBin.data(), responseBin.size());
        } else if (response->isOK()) {
            status = FAILURE(RadioError::GENERIC_FAILURE);
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        if (status == RadioError::NONE) {
            NOT_NULL(mRadioSimResponse)->requestIccSimAuthenticationResponse(
                    makeRadioResponseInfo(serial), std::move(iccIoResult));
            return true;
        } else {
            NOT_NULL(mRadioSimResponse)->requestIccSimAuthenticationResponse(
                    makeRadioResponseInfo(serial, status), {});
            return status != RadioError::INTERNAL_ERR;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::sendEnvelope(const int32_t serial,
                                     const std::string& contents) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, contents]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CUSATE = AtResponse::CUSATE;
        RadioError status = RadioError::NONE;
        std::string commandResponse;

        const std::string request = std::format("AT+CUSATE=\"{0:s}\"", contents);
        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CUSATE>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CUSATE* cusate = response->get_if<CUSATE>()) {
            commandResponse = cusate->response;
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioSimResponse)->sendEnvelopeResponse(
            makeRadioResponseInfo(serial, status), std::move(commandResponse));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::sendEnvelopeWithStatus(const int32_t serial,
                                               const std::string& /*contents*/) {
    NOT_NULL(mRadioSimResponse)->sendEnvelopeWithStatusResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::sendTerminalResponseToSim(const int32_t serial,
                                                  const std::string& commandResponse) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, commandResponse]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CUSATT = AtResponse::CUSATT;
        RadioError status = RadioError::NONE;

        const std::string request = std::format("AT+CUSATT=\"{0:s}\"", commandResponse);
        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CUSATT>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (!response->get_if<CUSATT>()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioSimResponse)->sendTerminalResponseToSimResponse(
                makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::setAllowedCarriers(const int32_t serial,
                                           const sim::CarrierRestrictions& /*carriers*/,
                                           const sim::SimLockMultiSimPolicy /*multiSimPolicy*/) {
    NOT_NULL(mRadioSimResponse)->setAllowedCarriersResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::setCarrierInfoForImsiEncryption(const int32_t serial,
                                                        const sim::ImsiEncryptionInfo& /*imsiEncryptionInfo*/) {
    NOT_NULL(mRadioSimResponse)->setCarrierInfoForImsiEncryptionResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::setCdmaSubscriptionSource(const int32_t serial,
                                                  const sim::CdmaSubscriptionSource cdmaSub) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, cdmaSub]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        RadioError status = RadioError::NONE;

        const std::string request =
            std::format("AT+CCSS={0:d}", static_cast<unsigned>(cdmaSub));
        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioSimResponse)->setCdmaSubscriptionSourceResponse(
            makeRadioResponseInfo(serial, status));
        if ((status == RadioError::NONE) && mRadioSimIndication) {
            mRadioSimIndication->cdmaSubscriptionSourceChanged(
                RadioIndicationType::UNSOLICITED, cdmaSub);
        }

        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::setFacilityLockForApp(const int32_t serial,
                                              const std::string& facility,
                                              const bool lockState,
                                              const std::string& passwd,
                                              const int32_t serviceClass,
                                              const std::string& /*appId*/) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, facility, lockState,
                                passwd, serviceClass]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;
        int retry = 1;
        const int lockStateInt = lockState ? 1 : 0;

        std::string request;
        if (serviceClass == 0) {
            request = std::format("AT+CLCK=\"{0:s}\",{1:d},\"{2:s}\"",
                                  facility, lockStateInt, passwd);
        } else {
            request = std::format("AT+CLCK=\"{0:s}\",{1:d},\"{2:s}\",{3:d}",
                                  facility, lockStateInt, passwd, serviceClass);
        }

        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (response->get_if<CmeError>()) {
            if (facility.compare("SC"sv) == 0) {
                const std::optional<int> maybeRetries =
                    getRemainingRetries("SIM PIN"sv, requestPipe, mAtConversation);
                if (maybeRetries) {
                    status = FAILURE(RadioError::PASSWORD_INCORRECT);
                    retry = maybeRetries.value();
                } else {
                    status = FAILURE(RadioError::INTERNAL_ERR);
                }
            } else if (facility.compare("FD"sv) == 0) {
                const std::optional<int> maybeRetries =
                    getRemainingRetries("SIM PIN2"sv, requestPipe, mAtConversation);
                if (maybeRetries) {
                    status = FAILURE(RadioError::PASSWORD_INCORRECT);
                    retry = maybeRetries.value();
                } else {
                    status = FAILURE(RadioError::INTERNAL_ERR);
                }
            } else {
                status = FAILURE(RadioError::INVALID_ARGUMENTS);
                retry = -1;
            }
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioSimResponse)->setFacilityLockForAppResponse(
            makeRadioResponseInfo(serial, status), retry);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::setSimCardPower(const int32_t serial,
                                        sim::CardPowerState /*powerUp*/) {
    NOT_NULL(mRadioSimResponse)->setSimCardPowerResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::setUiccSubscription(const int32_t serial,
                                            const sim::SelectUiccSub& /*uiccSub*/) {
    NOT_NULL(mRadioSimResponse)->setUiccSubscriptionResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::supplyIccPin2ForApp(int32_t serial,
                                            const std::string& pin2,
                                            const std::string& /*aid*/) {
    mAtChannel->queueRequester([this, serial, pin2]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        const auto [status, remainingRetries] =
            enterOrChangeSimPinPuk(false, pin2, "", "SIM PIN2"sv,
                                   requestPipe, mAtConversation);

        NOT_NULL(mRadioSimResponse)->supplyIccPin2ForAppResponse(
                makeRadioResponseInfo(serial, status), remainingRetries);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::supplyIccPinForApp(int32_t serial,
                                           const std::string& pin,
                                           const std::string& /*aid*/) {
    mAtChannel->queueRequester([this, serial, pin]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        const auto [status, remainingRetries] =
            enterOrChangeSimPinPuk(false, pin, "", "SIM PIN"sv,
                                   requestPipe, mAtConversation);

        NOT_NULL(mRadioSimResponse)->supplyIccPinForAppResponse(
                makeRadioResponseInfo(serial, status), remainingRetries);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::supplyIccPuk2ForApp(int32_t serial,
                                            const std::string& puk2,
                                            const std::string& pin2,
                                            const std::string& /*aid*/) {
    mAtChannel->queueRequester([this, serial, puk2, pin2]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        const auto [status, remainingRetries] =
            enterOrChangeSimPinPuk(true, puk2, pin2, "SIM PUK2"sv,
                                   requestPipe, mAtConversation);

        NOT_NULL(mRadioSimResponse)->supplyIccPuk2ForAppResponse(
                makeRadioResponseInfo(serial, status), remainingRetries);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::supplyIccPukForApp(const int32_t serial,
                                           const std::string& puk,
                                           const std::string& pin,
                                           const std::string& /*aid*/) {
    mAtChannel->queueRequester([this, serial, puk, pin]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        const auto [status, remainingRetries] =
            enterOrChangeSimPinPuk(true, puk, pin, "SIM PUK"sv,
                                   requestPipe, mAtConversation);

        NOT_NULL(mRadioSimResponse)->supplyIccPukForAppResponse(
                makeRadioResponseInfo(serial, status), remainingRetries);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::supplySimDepersonalization(const int32_t serial,
                                                   sim::PersoSubstate /*persoType*/,
                                                   const std::string& /*controlKey*/) {
    NOT_NULL(mRadioSimResponse)->supplySimDepersonalizationResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__),
        {}, 0);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::updateSimPhonebookRecords(const int32_t serial,
                                                  const sim::PhonebookRecordInfo& /*recordInfo*/) {
    NOT_NULL(mRadioSimResponse)->updateSimPhonebookRecordsResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__), 0);
    return ScopedAStatus::ok();
}

void RadioSim::handleUnsolicited(const AtResponse::CFUN& cfun) {
    bool changed;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        changed = mRadioState != cfun.state;
        mRadioState = cfun.state;
    }

    if (changed && mRadioSimIndication) {
        mRadioSimIndication->simStatusChanged(
            RadioIndicationType::UNSOLICITED);

        mRadioSimIndication->subscriptionStatusChanged(
            RadioIndicationType::UNSOLICITED, mRadioState == modem::RadioState::ON);
    }
}

void RadioSim::handleUnsolicited(const AtResponse::CUSATP& cusatp) {
    const std::string& cmd = cusatp.cmd;
    if (cmd.size() < 3) {
        return;
    }
    const unsigned typeOffset = (cmd[2] <= '7') ? 10 : 12;
    if (cmd.size() < (typeOffset + 2)) {
        return;
    }

    unsigned cmdType = 0;
    if (!(std::from_chars(&cmd[typeOffset], &cmd[typeOffset + 2], cmdType, 16).ec == std::errc{})) {
        return;
    }

    const StkCmdType stkCmdType = static_cast<StkCmdType>(cmdType);

    enum class Action {
        NOTHING, NOTIFY, PROACTIVE_CMD
    };

    Action action;

    {
        std::lock_guard<std::mutex> lock(mMtx);

        switch (stkCmdType) {
        case StkCmdType::RUN_AT:
        case StkCmdType::SEND_DTMF:
        case StkCmdType::SEND_SMS:
        case StkCmdType::SEND_SS:
        case StkCmdType::SEND_USSD:
        case StkCmdType::PLAY_TONE:
        case StkCmdType::CLOSE_CHANNEL:
            action = Action::NOTIFY;
            break;

        case StkCmdType::REFRESH:
            if (cmd.size() >= (typeOffset + 4) && !strncmp(&cmd[typeOffset + 2], "04", 2)) {
                // SIM_RESET
                mStkServiceRunning = false;
                action = Action::NOTHING;
            } else {
                action = Action::NOTIFY;
            }
            break;

        default:
            action = Action::PROACTIVE_CMD;
            break;
        }

        if (!mStkServiceRunning) {
            mStkUnsolResponse = cusatp;
            action = Action::NOTHING;
        }
    }

    if (mRadioSimIndication) {
        switch (action) {
        case Action::NOTIFY:
            mRadioSimIndication->stkEventNotify(RadioIndicationType::UNSOLICITED, cmd);
            break;

        case Action::PROACTIVE_CMD:
            mRadioSimIndication->stkProactiveCommand(RadioIndicationType::UNSOLICITED, cmd);
            break;

        case Action::NOTHING:
            break;
        }
    }
}

void RadioSim::handleUnsolicited(const AtResponse::CUSATEND&) {
    if (mRadioSimIndication) {
        mRadioSimIndication->stkSessionEnd(RadioIndicationType::UNSOLICITED);
    }
}

void RadioSim::atResponseSink(const AtResponsePtr& response) {
    if (!mAtConversation.send(response)) {
        response->visit([this](const auto& msg){ handleUnsolicited(msg); });
    }
}

ScopedAStatus RadioSim::responseAcknowledgement() {
    return ScopedAStatus::ok();
}

ScopedAStatus RadioSim::setResponseFunctions(
        const std::shared_ptr<sim::IRadioSimResponse>& radioSimResponse,
        const std::shared_ptr<sim::IRadioSimIndication>& radioSimIndication) {
    mRadioSimResponse = NOT_NULL(radioSimResponse);
    mRadioSimIndication = NOT_NULL(radioSimIndication);
    return ScopedAStatus::ok();
}

/************************* deprecated *************************/
ScopedAStatus RadioSim::iccCloseLogicalChannel(const int32_t serial,
                                               const int32_t /*channelId*/) {
    NOT_NULL(mRadioSimResponse)->iccCloseLogicalChannelResponse(
        makeRadioResponseInfoDeprecated(serial));
    return ScopedAStatus::ok();
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
