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
#include <mutex>

#include <aidl/android/hardware/radio/sim/BnRadioSim.h>
#include "AtChannel.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
using ::ndk::ScopedAStatus;

struct RadioSim : public sim::BnRadioSim {
    RadioSim(std::shared_ptr<AtChannel> atChannel);

    ScopedAStatus areUiccApplicationsEnabled(int32_t serial) override;
    ScopedAStatus changeIccPin2ForApp(int32_t serial, const std::string& oldPin2,
                                      const std::string& newPin2,
                                      const std::string& aid) override;
    ScopedAStatus changeIccPinForApp(int32_t serial, const std::string& oldPin,
                                     const std::string& newPin,
                                     const std::string& aid) override;
    ScopedAStatus enableUiccApplications(int32_t serial, bool enable) override;
    ScopedAStatus getAllowedCarriers(int32_t serial) override;
    ScopedAStatus getCdmaSubscription(int32_t serial) override;
    ScopedAStatus getCdmaSubscriptionSource(int32_t serial) override;
    ScopedAStatus getFacilityLockForApp(int32_t serial, const std::string& facility,
                                        const std::string& password, int32_t serviceClass,
                                        const std::string& appId) override;
    ScopedAStatus getIccCardStatus(int32_t serial) override;
    ScopedAStatus getImsiForApp(int32_t serial, const std::string& aid) override;
    ScopedAStatus getSimPhonebookCapacity(int32_t serial) override;
    ScopedAStatus getSimPhonebookRecords(int32_t serial) override;
    ScopedAStatus iccCloseLogicalChannel(int32_t serial, int32_t channelId) override;
    ScopedAStatus iccCloseLogicalChannelWithSessionInfo(int32_t serial,
            const sim::SessionInfo& recordInfo) override;
    ScopedAStatus iccIoForApp(int32_t serial, const sim::IccIo& iccIo) override;
    ScopedAStatus iccOpenLogicalChannel(int32_t serial, const std::string& aid,
                                        int32_t p2) override;
    ScopedAStatus iccTransmitApduBasicChannel(
            int32_t serial, const sim::SimApdu& message) override;
    ScopedAStatus iccTransmitApduLogicalChannel(
            int32_t serial, const sim::SimApdu& message) override;
    ScopedAStatus reportStkServiceIsRunning(int32_t serial) override;
    ScopedAStatus requestIccSimAuthentication(int32_t serial, int32_t authContext,
                                              const std::string& authData,
                                              const std::string& aid) override;
    ScopedAStatus sendEnvelope(int32_t serial, const std::string& command) override;
    ScopedAStatus sendEnvelopeWithStatus(int32_t serial,
                                         const std::string& contents) override;
    ScopedAStatus sendTerminalResponseToSim(int32_t serial,
                                            const std::string& commandResponse) override;
    ScopedAStatus setAllowedCarriers(
            int32_t serial, const sim::CarrierRestrictions& carriers,
            sim::SimLockMultiSimPolicy multiSimPolicy) override;
    ScopedAStatus setCarrierInfoForImsiEncryption(
            int32_t serial, const sim::ImsiEncryptionInfo& imsiEncryptionInfo)
            override;
    ScopedAStatus setCdmaSubscriptionSource(
            int32_t serial, sim::CdmaSubscriptionSource cdmaSub) override;
    ScopedAStatus setFacilityLockForApp(
            int32_t serial, const std::string& facility,
            bool lockState, const std::string& passwd,
            int32_t serviceClass, const std::string& appId) override;
    ScopedAStatus setSimCardPower(int32_t serial, sim::CardPowerState powerUp) override;
    ScopedAStatus setUiccSubscription(
            int32_t serial, const sim::SelectUiccSub& uiccSub) override;
    ScopedAStatus supplyIccPin2ForApp(int32_t serial, const std::string& pin2,
                                      const std::string& aid) override;
    ScopedAStatus supplyIccPinForApp(int32_t serial, const std::string& pin,
                                     const std::string& aid) override;
    ScopedAStatus supplyIccPuk2ForApp(int32_t serial, const std::string& puk2,
                                      const std::string& pin2,
                                      const std::string& aid) override;
    ScopedAStatus supplyIccPukForApp(int32_t serial, const std::string& puk,
                                     const std::string& pin,
                                     const std::string& aid) override;
    ScopedAStatus supplySimDepersonalization(
            int32_t serial, sim::PersoSubstate persoType, const std::string& controlKey) override;
    ScopedAStatus updateSimPhonebookRecords(
            int32_t serial, const sim::PhonebookRecordInfo& recordInfo) override;

    void atResponseSink(const AtResponsePtr& response);
    void handleUnsolicited(const AtResponse::CFUN&);
    void handleUnsolicited(const AtResponse::CUSATP&);
    void handleUnsolicited(const AtResponse::CUSATEND&);
    template <class IGNORE> void handleUnsolicited(const IGNORE&) {}

    ScopedAStatus responseAcknowledgement() override;
    ScopedAStatus setResponseFunctions(
            const std::shared_ptr<sim::IRadioSimResponse>& radioSimResponse,
            const std::shared_ptr<sim::IRadioSimIndication>& radioSimIndication) override;

private:
    std::shared_ptr<sim::IRadioSimResponse> mRadioSimResponse;
    std::shared_ptr<sim::IRadioSimIndication> mRadioSimIndication;

    const std::shared_ptr<AtChannel> mAtChannel;
    AtChannel::Conversation mAtConversation;

    std::mutex mMtx;
    std::optional<AtResponse::CUSATP> mStkUnsolResponse;
    modem::RadioState mRadioState = modem::RadioState::OFF;
    // The modem simulator does not support SIM card power modes.
    sim::CardPowerState mCardPowerState = sim::CardPowerState::POWER_UP;
    bool mUiccApplicationsEnabled = true;
    bool mStkServiceRunning = false;
};

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
