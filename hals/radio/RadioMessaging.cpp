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

#define FAILURE_DEBUG_PREFIX "RadioMessaging"

#include "RadioMessaging.h"

#include "atCmds.h"
#include "debug.h"
#include "makeRadioResponseInfo.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
namespace {
using messaging::GsmBroadcastSmsConfigInfo;

using namespace std::literals;
constexpr std::string_view kCtrlZ = "\032"sv;
}  // namespace

RadioMessaging::RadioMessaging(std::shared_ptr<AtChannel> atChannel) : mAtChannel(std::move(atChannel)) {
}

ScopedAStatus RadioMessaging::acknowledgeIncomingGsmSmsWithPdu(const int32_t serial,
                                                               const bool /*success*/,
                                                               const std::string& /*ackPdu*/) {
    // unsupported in reference-ril.c
    NOT_NULL(mRadioMessagingResponse)->acknowledgeIncomingGsmSmsWithPduResponse(
        makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::acknowledgeLastIncomingCdmaSms(const int32_t serial,
                                                             const messaging::CdmaSmsAck& /*smsAck*/) {
    // unsupported in reference-ril.c
    NOT_NULL(mRadioMessagingResponse)->acknowledgeLastIncomingCdmaSmsResponse(
        makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::acknowledgeLastIncomingGsmSms(const int32_t serial,
                                                            const bool success,
                                                            const messaging::SmsAcknowledgeFailCause /*cause*/) {
    mAtChannel->queueRequester([this, serial, success]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        RadioError status = RadioError::NONE;

        AtResponsePtr response =
            mAtConversation(requestPipe, std::format("AT+CNMA={0:d}", (success ? 1 : 2)),
                            [](const AtResponse& response) -> bool {
                               return response.isOK();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        }

        NOT_NULL(mRadioMessagingResponse)->acknowledgeLastIncomingGsmSmsResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::deleteSmsOnRuim(const int32_t serial,
                                              const int32_t /*index*/) {
    NOT_NULL(mRadioMessagingResponse)->deleteSmsOnRuimResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::deleteSmsOnSim(const int32_t serial,
                                             const int32_t index) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, index]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;

        AtResponsePtr response =
            mAtConversation(requestPipe, std::format("AT+CMGD={0:d}", index),
                            [](const AtResponse& response) -> bool {
                               return response.isOK() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (response->get_if<CmeError>()) {
            status = FAILURE(RadioError::INVALID_ARGUMENTS);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioMessagingResponse)->deleteSmsOnSimResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::getCdmaBroadcastConfig(const int32_t serial) {
    NOT_NULL(mRadioMessagingResponse)->getCdmaBroadcastConfigResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::getGsmBroadcastConfig(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CSCB = AtResponse::CSCB;
        using messaging::GsmBroadcastSmsConfigInfo;

        RadioError status = RadioError::NONE;
        std::vector<GsmBroadcastSmsConfigInfo> gbsci;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getBroadcastConfig,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CSCB>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CSCB* cscb = response->get_if<CSCB>()) {
            const size_t size = std::min(cscb->serviceId.size(),
                                         cscb->codeScheme.size());
            gbsci.resize(size);

            const bool selected = (cscb->mode != 0);

            for (size_t i = 0; i < size; ++i) {
                gbsci[i].selected = selected;
                gbsci[i].fromServiceId = cscb->serviceId[i].from;
                gbsci[i].toServiceId = cscb->serviceId[i].to;
                gbsci[i].fromCodeScheme = cscb->codeScheme[i].from;
                gbsci[i].toCodeScheme = cscb->codeScheme[i].to;
            }
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioMessagingResponse)->getGsmBroadcastConfigResponse(
            makeRadioResponseInfo(serial, status), std::move(gbsci));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::getSmscAddress(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CSCA = AtResponse::CSCA;
        std::string smscAddress;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getSmscAddress,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CSCA>();
                            });
        if (!response || response->isParseError()) {
            NOT_NULL(mRadioMessagingResponse)->getSmscAddressResponse(
                makeRadioResponseInfo(serial, FAILURE(RadioError::INTERNAL_ERR)),
                "");
            return false;
        } else if (const CSCA* csca = response->get_if<CSCA>()) {
            smscAddress = csca->sca;
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioMessagingResponse)->getSmscAddressResponse(
            makeRadioResponseInfo(serial), std::move(smscAddress));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::reportSmsMemoryStatus(const int32_t serial,
                                                    const bool /*available*/) {
    NOT_NULL(mRadioMessagingResponse)->reportSmsMemoryStatusResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::sendCdmaSms(const int32_t serial,
                                          const messaging::CdmaSmsMessage& /*sms*/) {
    NOT_NULL(mRadioMessagingResponse)->sendCdmaSmsResponse(
        makeRadioResponseInfoUnsupported(  // reference-ril.c returns OK but does nothing
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::sendCdmaSmsExpectMore(const int32_t serial,
                                                    const messaging::CdmaSmsMessage& /*sms*/) {
    NOT_NULL(mRadioMessagingResponse)->sendCdmaSmsExpectMoreResponse(
        makeRadioResponseInfoUnsupported(  // reference-ril.c returns OK but does nothing
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::sendImsSms(const int32_t serial,
                                         const messaging::ImsSmsMessage& /*message*/) {
    NOT_NULL(mRadioMessagingResponse)->sendImsSmsResponse(
        makeRadioResponseInfoUnsupported(  // <TODO>
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

std::pair<RadioResponseInfo, messaging::SendSmsResult>
RadioMessaging::sendSmsImpl(const AtChannel::RequestPipe requestPipe,
                            const int serial,
                            const messaging::GsmSmsMessage &message) {
    using messaging::SendSmsResult;
    using SmsPrompt = AtResponse::SmsPrompt;
    using CMGS = AtResponse::CMGS;

    RadioError status = RadioError::NONE;
    std::string request = std::format("AT+CMGS={0:d}", message.pdu.size() / 2);

    AtResponsePtr response =
        mAtConversation(requestPipe, request,
                        [](const AtResponse& response) -> bool {
                           return response.holds<SmsPrompt>();
                        });
    if (!response || response->isParseError()) {
        status = FAILURE(RadioError::INTERNAL_ERR);
done:   return {makeRadioResponseInfo(serial, status), {}};
    } else if (!response->holds<SmsPrompt>()) {
        response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
    }

    SendSmsResult sendSmsResult;

    const std::string_view smsc =
        message.smscPdu.empty() ? "00"sv : std::string_view(message.smscPdu);

    request = std::format("{0:s}{1:s}{2:s}", smsc, message.pdu, kCtrlZ);
    response =
        mAtConversation(requestPipe, request,
                        [](const AtResponse& response) -> bool {
                           return response.holds<CMGS>();
                        });
    if (!response || response->isParseError()) {
        status = FAILURE(RadioError::INTERNAL_ERR);
        goto done;
    } else if (const CMGS* cmgs = response->get_if<CMGS>()) {
        sendSmsResult.messageRef = cmgs->messageRef;
    } else {
        response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
    }

    return {makeRadioResponseInfo(serial), std::move(sendSmsResult)};
}


ScopedAStatus RadioMessaging::sendSms(
        const int32_t serial, const messaging::GsmSmsMessage& message) {
    mAtChannel->queueRequester([this, serial, message]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        auto [response, sendSmsResult] = sendSmsImpl(requestPipe, serial, message);

        NOT_NULL(mRadioMessagingResponse)->sendSmsResponse(
            response, std::move(sendSmsResult));

        return response.error != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::sendSmsExpectMore(
        const int32_t serial, const messaging::GsmSmsMessage& message) {
    mAtChannel->queueRequester([this, serial, message]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        auto [response, sendSmsResult] = sendSmsImpl(requestPipe, serial, message);

        NOT_NULL(mRadioMessagingResponse)->sendSmsExpectMoreResponse(
            response, std::move(sendSmsResult));

        return response.error != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::setCdmaBroadcastConfig(const int32_t serial,
                                                     const std::vector<messaging::CdmaBroadcastSmsConfigInfo>& /*configInfo*/) {
    NOT_NULL(mRadioMessagingResponse)->setCdmaBroadcastConfigResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::setCdmaBroadcastActivation(const int32_t serial,
                                                         const bool /*activate*/) {
    NOT_NULL(mRadioMessagingResponse)->setCdmaBroadcastActivationResponse(
            makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::setGsmBroadcastConfig(
        int32_t serial, const std::vector<GsmBroadcastSmsConfigInfo>& configInfo) {
    if (configInfo.empty()) {
        NOT_NULL(mRadioMessagingResponse)->setGsmBroadcastConfigResponse(
            makeRadioResponseInfo(serial, FAILURE(RadioError::INVALID_ARGUMENTS)));
        return ScopedAStatus::ok();
    }

    const int mode = configInfo.front().selected ? 0 : 1;
    std::string channel;
    std::string language;

    for (const GsmBroadcastSmsConfigInfo& ci : configInfo) {
        if (!channel.empty()) {
            channel += ",";
            language += ",";
        }

        if (ci.fromServiceId == ci.toServiceId) {
            channel += std::to_string(ci.fromServiceId);
        } else {
            channel += std::format("{0:d}-{1:d}", ci.fromServiceId,
                                   ci.toServiceId);
        }

        if (ci.fromCodeScheme == ci.toCodeScheme) {
            language += std::to_string(ci.fromCodeScheme);
        } else {
            language += std::format("{0:d}-{1:d}", ci.fromCodeScheme,
                                    ci.toCodeScheme);
        }
    }

    std::string request = std::format("AT+CSCB={0:d},\"{1:s}\",\"{2:s}\"",
                                      mode, channel, language);

    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, request = std::move(request)]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using OK = AtResponse::OK;
        RadioError status = RadioError::NONE;

        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<OK>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (!response->holds<OK>()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioMessagingResponse)->setSmscAddressResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::setGsmBroadcastActivation(const int32_t serial,
                                                        const bool /*activate*/) {
    NOT_NULL(mRadioMessagingResponse)->setGsmBroadcastActivationResponse(
            makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::setSmscAddress(const int32_t serial, const std::string& smsc) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, smsc]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using OK = AtResponse::OK;
        RadioError status = RadioError::NONE;

        const std::string request = std::format("AT+CSCA={0:s},{1:d}", smsc, 0);
        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<OK>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (!response->holds<OK>()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioMessagingResponse)->setSmscAddressResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::writeSmsToRuim(const int32_t serial,
                                             const messaging::CdmaSmsWriteArgs& /*cdmaSms*/) {
    NOT_NULL(mRadioMessagingResponse)->writeSmsToRuimResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__), 0);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::writeSmsToSim(const int32_t serial,
                                            const messaging::SmsWriteArgs& smsWriteArgs) {
    mAtChannel->queueRequester([this, serial, smsWriteArgs]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using SmsPrompt = AtResponse::SmsPrompt;
        using CMGW = AtResponse::CMGW;

        RadioError status = RadioError::NONE;
        int messageRef = -1;

        std::string request = std::format("AT+CMGW=%d,%d",
            smsWriteArgs.pdu.size() / 2, smsWriteArgs.status);

        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<SmsPrompt>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
            goto done;
        } else if (!response->holds<SmsPrompt>()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
        }

        request = std::format("{0:s}{1:s}", smsWriteArgs.pdu, kCtrlZ);
        response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CMGW>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
            goto done;
        } else if (const CMGW* cmgw = response->get_if<CMGW>()) {
            messageRef = cmgw->messageRef;
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
        }

done:   NOT_NULL(mRadioMessagingResponse)->writeSmsToSimResponse(
            makeRadioResponseInfo(serial, status), messageRef);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

void RadioMessaging::atResponseSink(const AtResponsePtr& response) {
    if (!mAtConversation.send(response)) {
        response->visit([this](const auto& msg){ handleUnsolicited(msg); });
    }
}

void RadioMessaging::handleUnsolicited(const AtResponse::CMT& cmt) {
    if (mRadioMessagingIndication) {
        mRadioMessagingIndication->newSms(
            RadioIndicationType::UNSOLICITED, cmt.pdu);
    }
}

void RadioMessaging::handleUnsolicited(const AtResponse::CDS& cds) {
    if (mRadioMessagingIndication) {
        mRadioMessagingIndication->newSmsStatusReport(
            RadioIndicationType::UNSOLICITED, cds.pdu);
    }
}

ScopedAStatus RadioMessaging::responseAcknowledgement() {
    return ScopedAStatus::ok();
}

ScopedAStatus RadioMessaging::setResponseFunctions(
        const std::shared_ptr<messaging::IRadioMessagingResponse>& radioMessagingResponse,
        const std::shared_ptr<messaging::IRadioMessagingIndication>& radioMessagingIndication) {
    mRadioMessagingResponse = NOT_NULL(radioMessagingResponse);
    mRadioMessagingIndication = NOT_NULL(radioMessagingIndication);
    return ScopedAStatus::ok();
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
