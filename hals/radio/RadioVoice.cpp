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
    NOT_NULL(mRadioVoiceResponse)->acceptCallResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::cancelPendingUssd(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->cancelPendingUssdResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::conference(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->conferenceResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::dial(const int32_t serial,
                               const voice::Dial& /*dialInfo*/) {
    NOT_NULL(mRadioVoiceResponse)->dialResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::emergencyDial(const int32_t serial,
                                        const voice::Dial& /*dialInfo*/,
                                        const int32_t /*categories*/,
                                        const std::vector<std::string>& /*urns*/,
                                        const voice::EmergencyCallRouting /*routing*/,
                                        const bool /*hasKnownUserIntentEmergency*/,
                                        const bool /*isTesting*/) {
    NOT_NULL(mRadioVoiceResponse)->emergencyDialResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::exitEmergencyCallbackMode(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->exitEmergencyCallbackModeResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::explicitCallTransfer(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->explicitCallTransferResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
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
                               return response.holds<CCFCU>();
                                      response.isOK() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CCFCU* ccfcu = response->get_if<CCFCU>()) {
            callForwardInfos = ccfcu->callForwardInfos;
        } else if (response->get_if<CmeError>()) {
            status = FAILURE(RadioError::GENERIC_FAILURE);
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

        RadioError status = RadioError::NONE;
        bool enable = false;
        int serviceClassOut = -1;

        const std::string request =
            std::format("AT+CCWA={0:d},{1:d},{2:d}",
                        1, 2, serviceClass);
        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getCurrentCalls,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CCWA>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CCWA* ccwa = response->get_if<CCWA>()) {
            enable = ccwa->enable;
            serviceClassOut = ccwa->serviceClass;
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
    NOT_NULL(mRadioVoiceResponse)->getClipResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getClir(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->getClirResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__), 0, 0);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getCurrentCalls(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CLCC = AtResponse::CLCC;

        std::vector<voice::Call> calls;

        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getCurrentCalls,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CLCC>() || response.isOK();
                            });
        if (!response || response->isParseError()) {
             NOT_NULL(mRadioVoiceResponse)->getCurrentCallsResponse(
                    makeRadioResponseInfo(serial, FAILURE(RadioError::INTERNAL_ERR)), {});
            return false;
        } else if (const CLCC* clcc = response->get_if<CLCC>()) {
            calls = clcc->calls;
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioVoiceResponse)->getCurrentCallsResponse(
            makeRadioResponseInfo(serial), std::move(calls));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getLastCallFailCause(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->getLastCallFailCauseResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getMute(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->getMuteResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__), false);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::getPreferredVoicePrivacy(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->getPreferredVoicePrivacyResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
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
    NOT_NULL(mRadioVoiceResponse)->handleStkCallSetupRequestFromSimResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::hangup(const int32_t serial,
                                 const int32_t /*gsmIndex*/) {
    NOT_NULL(mRadioVoiceResponse)->hangupConnectionResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::hangupForegroundResumeBackground(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->hangupForegroundResumeBackgroundResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::hangupWaitingOrBackground(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->hangupWaitingOrBackgroundResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::isVoNrEnabled(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->isVoNrEnabledResponse(
            makeRadioResponseInfoNOP(serial), false);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::rejectCall(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->rejectCallResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::sendBurstDtmf(const int32_t serial,
                                        const std::string& /*dtmf*/,
                                        const int32_t /*on*/,
                                        const int32_t /*off*/) {
    NOT_NULL(mRadioVoiceResponse)->sendBurstDtmfResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::sendCdmaFeatureCode(const int32_t serial,
                                              const std::string& /*fcode*/) {
    NOT_NULL(mRadioVoiceResponse)->sendCdmaFeatureCodeResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::sendDtmf(const int32_t serial,
                                   const std::string& /*s*/) {
    NOT_NULL(mRadioVoiceResponse)->sendDtmfResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::sendUssd(const int32_t serial,
                                   const std::string& /*ussd*/) {
    NOT_NULL(mRadioVoiceResponse)->sendUssdResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::separateConnection(const int32_t serial,
                                             const int32_t /*gsmIndex*/) {
    NOT_NULL(mRadioVoiceResponse)->separateConnectionResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
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
        } else if (response->get_if<CmeError>()) {
            status = FAILURE(RadioError::GENERIC_FAILURE);
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
        RadioError status = RadioError::NONE;

        const std::string request =
            std::format("AT+CCWA={0:d},{1:d},{2:d}", 1, (enable ? 1 : 0),
                        serviceClass);
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
        RadioError status = RadioError::NONE;

        const std::string request = std::format("AT+CLIR: {0:d}", clirStatus);
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

        NOT_NULL(mRadioVoiceResponse)->setClirResponse(
                makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setMute(const int32_t serial,
                                  const bool /*enable*/) {
    NOT_NULL(mRadioVoiceResponse)->setMuteResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::setPreferredVoicePrivacy(const int32_t serial,
                                                   const bool /*enable*/) {
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
    NOT_NULL(mRadioVoiceResponse)->setVoNrEnabledResponse(
        makeRadioResponseInfo(serial, enable ?
            FAILURE(RadioError::REQUEST_NOT_SUPPORTED) : RadioError::NONE));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::startDtmf(const int32_t serial, const std::string& /*s*/) {
    NOT_NULL(mRadioVoiceResponse)->startDtmfResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::stopDtmf(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->stopDtmfResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioVoice::switchWaitingOrHoldingAndActive(const int32_t serial) {
    NOT_NULL(mRadioVoiceResponse)->switchWaitingOrHoldingAndActiveResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__));
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
