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

#include <aidl/android/hardware/radio/voice/BnRadioVoice.h>
#include "AtChannel.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
using ::ndk::ScopedAStatus;

struct RadioVoice : public voice::BnRadioVoice {
    RadioVoice(std::shared_ptr<AtChannel> atChannel);

    ScopedAStatus acceptCall(int32_t serial) override;
    ScopedAStatus cancelPendingUssd(int32_t serial) override;
    ScopedAStatus conference(int32_t serial) override;
    ScopedAStatus dial(
            int32_t serial, const voice::Dial& dialInfo) override;
    ScopedAStatus emergencyDial(
            int32_t serial, const voice::Dial& dialInfo,
            int32_t categories, const std::vector<std::string>& urns,
            voice::EmergencyCallRouting routing,
            bool hasKnownUserIntentEmergency, bool isTesting) override;
    ScopedAStatus exitEmergencyCallbackMode(int32_t serial) override;
    ScopedAStatus explicitCallTransfer(int32_t serial) override;
    ScopedAStatus getCallForwardStatus(
            int32_t serial,
            const voice::CallForwardInfo& callInfo) override;
    ScopedAStatus getCallWaiting(int32_t serial, int32_t serviceClass) override;
    ScopedAStatus getClip(int32_t serial) override;
    ScopedAStatus getClir(int32_t serial) override;
    ScopedAStatus getCurrentCalls(int32_t serial) override;
    ScopedAStatus getLastCallFailCause(int32_t serial) override;
    ScopedAStatus getMute(int32_t serial) override;
    ScopedAStatus getPreferredVoicePrivacy(int32_t serial) override;
    ScopedAStatus getTtyMode(int32_t serial) override;
    ScopedAStatus handleStkCallSetupRequestFromSim(int32_t serial, bool accept) override;
    ScopedAStatus hangup(int32_t serial, int32_t gsmIndex) override;
    ScopedAStatus hangupForegroundResumeBackground(int32_t serial) override;
    ScopedAStatus hangupWaitingOrBackground(int32_t serial) override;
    ScopedAStatus isVoNrEnabled(int32_t serial) override;
    ScopedAStatus rejectCall(int32_t serial) override;
    ScopedAStatus sendBurstDtmf(int32_t serial, const std::string& dtmf,
                                int32_t on, int32_t off) override;
    ScopedAStatus sendCdmaFeatureCode(int32_t serial, const std::string& fcode) override;
    ScopedAStatus sendDtmf(int32_t serial, const std::string& s) override;
    ScopedAStatus sendUssd(int32_t serial, const std::string& ussd) override;
    ScopedAStatus separateConnection(int32_t serial, int32_t gsmIndex) override;
    ScopedAStatus setCallForward(
            int32_t serial, const voice::CallForwardInfo& callInfo) override;
    ScopedAStatus setCallWaiting(int32_t serial, bool enable, int32_t serviceClass) override;
    ScopedAStatus setClir(int32_t serial, int32_t status) override;
    ScopedAStatus setMute(int32_t serial, bool enable) override;
    ScopedAStatus setPreferredVoicePrivacy(int32_t serial, bool enable) override;
    ScopedAStatus setTtyMode(int32_t serial, voice::TtyMode mode) override;
    ScopedAStatus setVoNrEnabled(int32_t serial, bool enable) override;
    ScopedAStatus startDtmf(int32_t serial, const std::string& s) override;
    ScopedAStatus stopDtmf(int32_t serial) override;
    ScopedAStatus switchWaitingOrHoldingAndActive(int32_t serial) override;

    void atResponseSink(const AtResponsePtr& response);
    void handleUnsolicited(const AtResponse::RING&);
    void handleUnsolicited(const AtResponse::WSOS&);
    template <class IGNORE> void handleUnsolicited(const IGNORE&) {}

    ScopedAStatus responseAcknowledgement() override;
    ScopedAStatus setResponseFunctions(
            const std::shared_ptr<voice::IRadioVoiceResponse>& radioVoiceResponse,
            const std::shared_ptr<voice::IRadioVoiceIndication>& radioVoiceIndication) override;

    const std::shared_ptr<AtChannel> mAtChannel;
    AtChannel::Conversation mAtConversation;
    std::shared_ptr<voice::IRadioVoiceResponse> mRadioVoiceResponse;
    std::shared_ptr<voice::IRadioVoiceIndication> mRadioVoiceIndication;
};

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
