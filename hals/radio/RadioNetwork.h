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

#include <aidl/android/hardware/radio/network/BnRadioNetwork.h>
#include "AtChannel.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
using ::ndk::ScopedAStatus;

struct RadioNetwork : public network::BnRadioNetwork {
    RadioNetwork(std::shared_ptr<AtChannel> atChannel);

    ScopedAStatus getAllowedNetworkTypesBitmap(int32_t serial) override;
    ScopedAStatus getAvailableBandModes(int32_t serial) override;
    ScopedAStatus getAvailableNetworks(int32_t serial) override;
    ScopedAStatus getBarringInfo(int32_t serial) override;
    ScopedAStatus getCdmaRoamingPreference(int32_t serial) override;
    ScopedAStatus getCellInfoList(int32_t serial) override;
    ScopedAStatus getDataRegistrationState(int32_t serial) override;
    ScopedAStatus getImsRegistrationState(int32_t serial) override;
    ScopedAStatus getNetworkSelectionMode(int32_t serial) override;
    ScopedAStatus getOperator(int32_t serial) override;
    ScopedAStatus getSignalStrength(int32_t serial) override;
    ScopedAStatus getSystemSelectionChannels(int32_t serial) override;
    ScopedAStatus getVoiceRadioTechnology(int32_t serial) override;
    ScopedAStatus getVoiceRegistrationState(int32_t serial) override;
    ScopedAStatus isNrDualConnectivityEnabled(int32_t serial) override;
    ScopedAStatus setAllowedNetworkTypesBitmap(int32_t serial,
                                               int32_t networkTypeBitmap) override;
    ScopedAStatus setBandMode(
            int32_t serial, network::RadioBandMode mode) override;
    ScopedAStatus setBarringPassword(int32_t serial, const std::string& facility,
                                     const std::string& oldPassword,
                                     const std::string& newPassword) override;
    ScopedAStatus setCdmaRoamingPreference(
            int32_t serial, network::CdmaRoamingType type) override;
    ScopedAStatus setCellInfoListRate(int32_t serial, int32_t rate) override;
    ScopedAStatus setIndicationFilter(int32_t serial, int32_t indicationFilter) override;
    ScopedAStatus setLinkCapacityReportingCriteria(
            int32_t serial, int32_t hysteresisMs, int32_t hysteresisDlKbps,
            int32_t hysteresisUlKbps, const std::vector<int32_t>& thresholdsDownlinkKbps,
            const std::vector<int32_t>& thresholdsUplinkKbps, AccessNetwork accessNetwork) override;
    ScopedAStatus setLocationUpdates(int32_t serial, bool enable) override;
    ScopedAStatus setNetworkSelectionModeAutomatic(int32_t serial) override;
    ScopedAStatus setNetworkSelectionModeManual(
            int32_t serial, const std::string& operatorNumeric, AccessNetwork ran) override;
    ScopedAStatus setNrDualConnectivityState(
            int32_t serial, network::NrDualConnectivityState nrSt) override;
    ScopedAStatus setSignalStrengthReportingCriteria(
            int32_t serial, const std::vector<network::SignalThresholdInfo>&
                    signalThresholdInfos) override;
    ScopedAStatus setSuppServiceNotifications(int32_t serial, bool enable) override;
    ScopedAStatus setSystemSelectionChannels(
            int32_t serial, bool specifyChannels,
            const std::vector<network::RadioAccessSpecifier>& specifiers) override;
    ScopedAStatus startNetworkScan(
            int32_t serial, const network::NetworkScanRequest& request) override;
    ScopedAStatus stopNetworkScan(int32_t serial) override;
    ScopedAStatus supplyNetworkDepersonalization(int32_t serial, const std::string& netPin) override;
    ScopedAStatus setUsageSetting(
            int32_t serial, network::UsageSetting usageSetting) override;
    ScopedAStatus getUsageSetting(int32_t serial) override;
    ScopedAStatus setEmergencyMode(
            int32_t serial, network::EmergencyMode emergencyMode) override;
    ScopedAStatus triggerEmergencyNetworkScan(
            int32_t serial, const network::EmergencyNetworkScanTrigger& scanTrigger) override;
    ScopedAStatus cancelEmergencyNetworkScan(int32_t serial, bool resetScan) override;
    ScopedAStatus exitEmergencyMode(int32_t serial) override;
    ScopedAStatus isN1ModeEnabled(int32_t serial) override;
    ScopedAStatus setN1ModeEnabled(int32_t serial, bool enable) override;
    ScopedAStatus setNullCipherAndIntegrityEnabled(int32_t serial, bool enabled) override;
    ScopedAStatus isNullCipherAndIntegrityEnabled(int32_t serial) override;
    ScopedAStatus isCellularIdentifierTransparencyEnabled(int32_t serial) override;
    ScopedAStatus setCellularIdentifierTransparencyEnabled(int32_t serial,
                                                           bool enabled) override;
    ScopedAStatus setSecurityAlgorithmsUpdatedEnabled(int32_t serial, bool enabled) override;
    ScopedAStatus isSecurityAlgorithmsUpdatedEnabled(int32_t serial) override;

    void atResponseSink(const AtResponsePtr& response);
    void handleUnsolicited(const AtResponse::CFUN&);
    void handleUnsolicited(const AtResponse::CREG&);
    void handleUnsolicited(const AtResponse::CGREG&);
    void handleUnsolicited(const AtResponse::CSQ&);
    void handleUnsolicited(const AtResponse::COPS&);
    void handleUnsolicited(const AtResponse::CTEC&);
    void handleUnsolicited(const AtResponse::CGFPCCFG&);
    void handleUnsolicited(const AtResponse::CTZV&);
    template <class IGNORE> void handleUnsolicited(const IGNORE&) {}

    ScopedAStatus responseAcknowledgement() override;
    ScopedAStatus setResponseFunctions(
            const std::shared_ptr<network::IRadioNetworkResponse>& radioNetworkResponse,
            const std::shared_ptr<network::IRadioNetworkIndication>& radioNetworkIndication) override;

private:
    const std::shared_ptr<AtChannel> mAtChannel;
    AtChannel::Conversation mAtConversation;
    std::shared_ptr<network::IRadioNetworkResponse> mRadioNetworkResponse;
    std::shared_ptr<network::IRadioNetworkIndication> mRadioNetworkIndication;

    std::mutex          mMtx;
    AtResponse::CREG    mCreg;
    AtResponse::CGREG   mCgreg;
    AtResponse::CSQ     mCsq;
    std::optional<AtResponse::COPS::OperatorInfo> mCurrentOperator;
    std::optional<ratUtils::ModemTechnology> mCurrentModemTech;
    AtResponse::CTZV    mCtzv;
    int64_t             mCtzvTimestamp;
    modem::RadioState   mRadioState = modem::RadioState::OFF;
    int                 mPrimaryBandwidth = 0;
    int                 mSecondaryBandwidth = 0;
    bool                mNullCipherAndIntegrityEnabled = false;
    bool                mIsCellularIdentifierTransparencyEnabled = false;
    bool                mSecurityAlgorithmsUpdatedEnabled = false;
    bool                mIsNrDualConnectivityEnabled = true;
    bool                mIsN1ModeEnabled = false;
};

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
