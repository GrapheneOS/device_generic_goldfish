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

#define FAILURE_DEBUG_PREFIX "RadioVoice"

#include "RadioVoice.h"

#include "atCmds.h"
#include "debug.h"
#include "makeRadioResponseInfo.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {

RadioVoice::RadioVoice(std::shared_ptr<AtChannel> atChannel) : mAtChannel(std::move(atChannel)) {
}

ScopedAStatus RadioVoice::acceptCall(const int32_t serial) {
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        requestPipe(atCmds::acceptCall);
        NOT_NULL(mRadioVoiceResponse)->acceptCallResponse(
            makeRadioResponseInfo(serial));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::cancelPendingUssd(const int32_t serial) {
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        requestPipe(atCmds::cancelUssd);
        NOT_NULL(mRadioVoiceResponse)->cancelPendingUssdResponse(
            makeRadioResponseInfo(serial));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::conference(const int32_t serial) {
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        requestPipe(atCmds::conference);
        NOT_NULL(mRadioVoiceResponse)->conferenceResponse(
            makeRadioResponseInfo(serial));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::dial(const int32_t serial,
                               const voice::Dial& dialInfo) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, dialInfo]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using namespace std::literals;
        using CmeError = AtResponse::CmeError;
        using voice::Dial;

        RadioError status = RadioError::NONE;

        std::string_view clir;
        switch (dialInfo.clir) {
        case Dial::CLIR_INVOCATION:
            clir = "I"sv;
            break;
        case Dial::CLIR_SUPPRESSION:
            clir = "i"sv;
            break;
        default:
        case Dial::CLIR_DEFAULT:
            // clir is the empty string
            break;
        }

        const std::string request = std::format("ATD{0:s}{1:s};",
            dialInfo.address, clir);

        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->dialResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::emergencyDial(const int32_t serial,
                                        const voice::Dial& dialInfo,
                                        const int32_t categories,
                                        const std::vector<std::string>& /*urns*/,
                                        const voice::EmergencyCallRouting routing,
                                        const bool /*hasKnownUserIntentEmergency*/,
                                        const bool /*isTesting*/) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, dialInfo, categories, routing]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using namespace std::literals;
        using CmeError = AtResponse::CmeError;
        using voice::Dial;
        using voice::EmergencyCallRouting;

        RadioError status = RadioError::NONE;

        std::string_view clir;
        switch (dialInfo.clir) {
        case Dial::CLIR_INVOCATION:
            clir = "I"sv;
            break;
        case Dial::CLIR_SUPPRESSION:
            clir = "i"sv;
            break;
        default:
        case Dial::CLIR_DEFAULT:
            // clir is the empty string
            break;
        }

        std::string request;
        switch (routing) {
        case EmergencyCallRouting::EMERGENCY:
        case EmergencyCallRouting::UNKNOWN:
            if (categories) {
                request = std::format("ATD{0:s}@{1:d},#{2:s};",
                                      dialInfo.address, categories, clir);
            } else {
                request = std::format("ATD{0:s}@,#{1:s};", dialInfo.address, clir);
            }
            break;

        default:
        case EmergencyCallRouting::NORMAL:
            request = std::format("ATD{0:s}{1:s};", dialInfo.address, clir);
            break;
        }

        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->emergencyDialResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::exitEmergencyCallbackMode(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;

        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::exitEmergencyMode,
                            [](const AtResponse& response) -> bool {
                               return response.isOK() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->exitEmergencyCallbackModeResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::explicitCallTransfer(const int32_t serial) {
    // matches reference-ril.c
    NOT_NULL(mRadioVoiceResponse)->explicitCallTransferResponse(
        makeRadioResponseInfoUnsupported(
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getCallForwardStatus(const int32_t serial,
                                               const voice::CallForwardInfo& callInfo) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, callInfo](const AtChannel::RequestPipe requestPipe) -> bool {
        using CCFCU = AtResponse::CCFCU;
        using CmeError = AtResponse::CmeError;
        using voice::CallForwardInfo;

        RadioError status = RadioError::NONE;
        std::vector<CallForwardInfo> callForwardInfos;

        const std::string request = std::format(
                "AT+CCFCU={0:d},{1:d},{2:d},{3:d},\"{4:s}\",{5:d}",
                callInfo.reason, 2, 2, callInfo.toa,
                callInfo.number, callInfo.serviceClass);

        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CCFCU>() ||
                                      response.isOK() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CCFCU* ccfcu = response->get_if<CCFCU>()) {
            callForwardInfos = ccfcu->callForwardInfos;
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->getCallForwardStatusResponse(
                makeRadioResponseInfo(serial, status),
                std::move(callForwardInfos));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getCallWaiting(const int32_t serial,
                                         const int32_t serviceClass) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, serviceClass]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CCWA = AtResponse::CCWA;
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;
        bool enable = false;
        int serviceClassOut = -1;

        const std::string request =
            std::format("AT+CCWA={0:d},{1:d},{2:d}",
                        1, 2, serviceClass);
        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CCWA>() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CCWA* ccwa = response->get_if<CCWA>()) {
            enable = ccwa->enable;
            serviceClassOut = ccwa->serviceClass;
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->getCallWaitingResponse(
            makeRadioResponseInfo(serial, status), enable, serviceClassOut);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getClip(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CLIP = AtResponse::CLIP;
        using CmeError = AtResponse::CmeError;
        using ClipStatus = voice::ClipStatus;

        RadioError status = RadioError::NONE;
        ClipStatus clipStatus = ClipStatus::UNKNOWN;

        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getClip,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CLIP>() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CLIP* clip = response->get_if<CLIP>()) {
            clipStatus = clip->status;
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->getClipResponse(
            makeRadioResponseInfo(serial, status), clipStatus);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getClir(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CLIR = AtResponse::CLIR;
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;
        int n = -1;
        int m = -1;

        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getClir,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CLIR>() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CLIR* clir = response->get_if<CLIR>()) {
            n = clir->n;
            m = clir->m;
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->getClirResponse(
            makeRadioResponseInfo(serial, status), n, m);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getCurrentCalls(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CLCC = AtResponse::CLCC;
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;
        std::vector<voice::Call> calls;

        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getCurrentCalls,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CLCC>() ||
                                      response.isOK() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CLCC* clcc = response->get_if<CLCC>()) {
            calls = clcc->calls;
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->getCurrentCallsResponse(
            makeRadioResponseInfo(serial, status), std::move(calls));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getLastCallFailCause(const int32_t serial) {
    // matches reference-ril.c
    NOT_NULL(mRadioVoiceResponse)->getLastCallFailCauseResponse(
        makeRadioResponseInfoUnsupported(
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getMute(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CMUT = AtResponse::CMUT;
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;
        bool isMuted = false;

        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getCurrentCalls,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CMUT>() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
             NOT_NULL(mRadioVoiceResponse)->getCurrentCallsResponse(
                    makeRadioResponseInfo(serial, FAILURE(RadioError::INTERNAL_ERR)), {});
            return false;
        } else if (const CMUT* cmut = response->get_if<CMUT>()) {
            isMuted = cmut->on;
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }


        NOT_NULL(mRadioVoiceResponse)->getMuteResponse(
            makeRadioResponseInfo(serial, status), isMuted);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getPreferredVoicePrivacy(const int32_t serial) {
    // matches reference-ril.c
    NOT_NULL(mRadioVoiceResponse)->getPreferredVoicePrivacyResponse(
        makeRadioResponseInfoUnsupported(
            serial, FAILURE_DEBUG_PREFIX, __func__), false);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getTtyMode(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->getTtyModeResponse(
        makeRadioResponseInfo(serial), voice::TtyMode::FULL);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::handleStkCallSetupRequestFromSim(const int32_t serial,
                                                           const bool /*accept*/) {
    // matches reference-ril.c
    NOT_NULL(mRadioVoiceResponse)->handleStkCallSetupRequestFromSimResponse(
        makeRadioResponseInfoUnsupported(
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::hangup(const int32_t serial,
                                 const int32_t gsmIndex) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, gsmIndex]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        requestPipe(std::format("AT+CHLD=1{0:d}", gsmIndex));
        NOT_NULL(mRadioVoiceResponse)->hangupConnectionResponse(
            makeRadioResponseInfo(serial));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::hangupForegroundResumeBackground(const int32_t serial) {
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        requestPipe(atCmds::hangupForeground);
        NOT_NULL(mRadioVoiceResponse)->hangupForegroundResumeBackgroundResponse(
            makeRadioResponseInfo(serial));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::hangupWaitingOrBackground(const int32_t serial) {
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        requestPipe(atCmds::hangupWaiting);
        NOT_NULL(mRadioVoiceResponse)->hangupWaitingOrBackgroundResponse(
            makeRadioResponseInfo(serial));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::isVoNrEnabled(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->isVoNrEnabledResponse(
            makeRadioResponseInfoNOP(serial), false);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::rejectCall(const int32_t serial) {
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        requestPipe(atCmds::rejectCall);
        NOT_NULL(mRadioVoiceResponse)->rejectCallResponse(
            makeRadioResponseInfo(serial));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::sendBurstDtmf(const int32_t serial,
                                        const std::string& /*dtmf*/,
                                        const int32_t /*on*/,
                                        const int32_t /*off*/) {
    // matches reference-ril.c
    NOT_NULL(mRadioVoiceResponse)->sendBurstDtmfResponse(
        makeRadioResponseInfoUnsupported(
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::sendCdmaFeatureCode(const int32_t serial,
                                              const std::string& /*fcode*/) {
    // matches reference-ril.c
    NOT_NULL(mRadioVoiceResponse)->sendCdmaFeatureCodeResponse(
        makeRadioResponseInfoUnsupported(
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::sendDtmf(const int32_t serial,
                                   const std::string& s) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, s]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        requestPipe(std::format("AT+VTS={0:s}", s));
        NOT_NULL(mRadioVoiceResponse)->sendDtmfResponse(
            makeRadioResponseInfo(serial));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::sendUssd(const int32_t serial,
                                   const std::string& ussd) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, ussd]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        requestPipe(std::format("AT+CUSD=1,\"%s\"", ussd));
        NOT_NULL(mRadioVoiceResponse)->sendUssdResponse(
            makeRadioResponseInfo(serial));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::separateConnection(const int32_t serial,
                                             const int32_t gsmIndex) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, gsmIndex]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        if ((gsmIndex > 0) && (gsmIndex < 10)) {
            requestPipe(std::format("AT+CHLD=2{0:d}", gsmIndex));
            NOT_NULL(mRadioVoiceResponse)->separateConnectionResponse(
                makeRadioResponseInfo(serial));
        } else {
            NOT_NULL(mRadioVoiceResponse)->separateConnectionResponse(
                makeRadioResponseInfo(serial, FAILURE(RadioError::GENERIC_FAILURE)));
        }

        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setCallForward(const int32_t serial,
                                         const voice::CallForwardInfo& callInfo) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, callInfo](const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;

        std::string request = std::format(
                "AT+CCFCU={0:d},{1:d},{2:d},{3:d},\"{4:s}\",{5:d}",
                callInfo.reason, callInfo.status, 2, callInfo.toa,
                callInfo.number, callInfo.serviceClass);
        if ((callInfo.timeSeconds > 0) && (callInfo.status == 3)) {
            request += std::format(",\"\",\"\",,{0:d}", callInfo.timeSeconds);
        } else if (callInfo.serviceClass) {
            request += ",\"\"";
        }

        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->setCallForwardResponse(
                makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setCallWaiting(const int32_t serial,
                                         const bool enable,
                                         const int32_t serviceClass) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, enable, serviceClass]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;

        const std::string request =
            std::format("AT+CCWA={0:d},{1:d},{2:d}", 1, (enable ? 1 : 0),
                        serviceClass);
        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->setCallWaitingResponse(
                makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });


    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setClir(const int32_t serial, const int32_t clirStatus) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, clirStatus]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;

        const std::string request = std::format("AT+CLIR: {0:d}", clirStatus);
        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->setClirResponse(
                makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setMute(const int32_t serial,
                                  const bool enable) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, enable]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;
        RadioError status = RadioError::NONE;

        const std::string request =
            std::format("AT+CMUT={0:d}", (enable ? 1 : 0));
        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
             NOT_NULL(mRadioVoiceResponse)->getCurrentCallsResponse(
                    makeRadioResponseInfo(serial, FAILURE(RadioError::INTERNAL_ERR)), {});
            return false;
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->setMuteResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setPreferredVoicePrivacy(const int32_t serial,
                                                   const bool /*enable*/) {
    // matches reference-ril.c
    NOT_NULL(mRadioVoiceResponse)->setPreferredVoicePrivacyResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setTtyMode(const int32_t serial, voice::TtyMode /*mode*/) {
    NOT_NULL(mRadioVoiceResponse)->setTtyModeResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setVoNrEnabled(const int32_t serial, const bool enable) {
    // matches reference-ril.c
    NOT_NULL(mRadioVoiceResponse)->setVoNrEnabledResponse(
        makeRadioResponseInfo(serial, enable ?
            FAILURE(RadioError::REQUEST_NOT_SUPPORTED) : RadioError::NONE));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::startDtmf(const int32_t serial, const std::string& /*s*/) {
    // matches reference-ril.c
    NOT_NULL(mRadioVoiceResponse)->startDtmfResponse(
        makeRadioResponseInfoUnsupported(
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::stopDtmf(const int32_t serial) {
    // matches reference-ril.c
    NOT_NULL(mRadioVoiceResponse)->stopDtmfResponse(
        makeRadioResponseInfoUnsupported(
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::switchWaitingOrHoldingAndActive(const int32_t serial) {
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        requestPipe(atCmds::switchWaiting);
        NOT_NULL(mRadioVoiceResponse)->switchWaitingOrHoldingAndActiveResponse(
            makeRadioResponseInfo(serial));
        return true;
    });
    return ScopedAStatus::ok();
}

void RadioVoice::atResponseSink(const AtResponsePtr& response) {
    if (!mAtConversation.send(response)) {
        response->visit([this](const auto& msg){ handleUnsolicited(msg); });
    }
}

void RadioVoice::handleUnsolicited(const AtResponse::RING&) {
    if (mRadioVoiceIndication) {
        mRadioVoiceIndication->callRing(RadioIndicationType::UNSOLICITED, true, {});
        mRadioVoiceIndication->callStateChanged(RadioIndicationType::UNSOLICITED);
    }
}

void RadioVoice::handleUnsolicited(const AtResponse::WSOS& wsos) {
    if (mRadioVoiceIndication) {
        if (wsos.isEmergencyMode) {
            mRadioVoiceIndication->enterEmergencyCallbackMode(
                RadioIndicationType::UNSOLICITED);
        } else {
            mRadioVoiceIndication->exitEmergencyCallbackMode(
                RadioIndicationType::UNSOLICITED);
        }
    }
}

ScopedAStatus RadioVoice::responseAcknowledgement() {
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setResponseFunctions(
        const std::shared_ptr<voice::IRadioVoiceResponse>& radioVoiceResponse,
        const std::shared_ptr<voice::IRadioVoiceIndication>& radioVoiceIndication) {
    mRadioVoiceResponse = NOT_NULL(radioVoiceResponse);
    mRadioVoiceIndication = NOT_NULL(radioVoiceIndication);
    return ScopedAStatus::ok();
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
