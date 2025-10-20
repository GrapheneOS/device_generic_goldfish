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

#define FAILURE_DEBUG_PREFIX "RadioIms"

#include "RadioIms.h"
#include "debug.h"
#include "makeRadioResponseInfo.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {

RadioIms::RadioIms(std::shared_ptr<AtChannel> /*atChannel*/) {
}

ScopedAStatus RadioIms::setSrvccCallInfo(
        const int32_t serial, const std::vector<ims::SrvccCall>& /*srvccCalls*/) {
    NOT_NULL(mRadioImsResponse)->setSrvccCallInfoResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioIms::updateImsRegistrationInfo(
        const int32_t serial, const ims::ImsRegistration& /*imsRegistration*/) {
    NOT_NULL(mRadioImsResponse)->updateImsRegistrationInfoResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioIms::startImsTraffic(
        const int32_t serial, int32_t /*token*/, ims::ImsTrafficType /*imsTrafficType*/,
        AccessNetwork /*accessNetworkType*/, ims::ImsCall::Direction /*trafficDirection*/) {
    NOT_NULL(mRadioImsResponse)->startImsTrafficResponse(
        makeRadioResponseInfoNOP(serial), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioIms::stopImsTraffic(const int32_t serial, int32_t /*token*/) {
    NOT_NULL(mRadioImsResponse)->stopImsTrafficResponse(
            makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioIms::triggerEpsFallback(
        const int32_t serial, ims::EpsFallbackReason /*reason*/) {
    NOT_NULL(mRadioImsResponse)->triggerEpsFallbackResponse(
            makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioIms::sendAnbrQuery(
        const int32_t serial, ims::ImsStreamType /*mediaType*/,
        ims::ImsStreamDirection /*direction*/, int32_t /*bitsPerSecond*/) {
    NOT_NULL(mRadioImsResponse)->sendAnbrQueryResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioIms::updateImsCallStatus(
        const int32_t serial, const std::vector<ims::ImsCall>& /*imsCalls*/) {
    NOT_NULL(mRadioImsResponse)->updateImsCallStatusResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioIms::updateAllowedServices(
        const int32_t serial,
        const std::vector<ims::ImsService>& /*imsServices*/) {
    NOT_NULL(mRadioImsResponse)->updateAllowedServicesResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

void RadioIms::atResponseSink(const AtResponsePtr& /*response*/) {}

ScopedAStatus RadioIms::setResponseFunctions(
        const std::shared_ptr<ims::IRadioImsResponse>& radioImsResponse,
        const std::shared_ptr<ims::IRadioImsIndication>& radioImsIndication) {
    mRadioImsResponse = NOT_NULL(radioImsResponse);
    mRadioImsIndication = NOT_NULL(radioImsIndication);
    return ScopedAStatus::ok();
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
