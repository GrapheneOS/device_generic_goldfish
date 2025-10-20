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
#include <unordered_map>
#include <unordered_set>

#include <aidl/android/hardware/radio/data/BnRadioData.h>
#include "AtChannel.h"
#include "IdAllocator.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
using ::ndk::ScopedAStatus;

struct RadioData : public data::BnRadioData {
    RadioData(std::shared_ptr<AtChannel> atChannel);

    ScopedAStatus getSlicingConfig(int32_t serial) override;

    ScopedAStatus setDataAllowed(int32_t serial, bool allow) override;
    ScopedAStatus setDataProfile(
            int32_t serial,
            const std::vector<data::DataProfileInfo>& profiles)
            override;
    ScopedAStatus setDataThrottling(
            int32_t serial,
            data::DataThrottlingAction dataThrottlingAction,
            int64_t completionDurationMillis) override;
    ScopedAStatus setInitialAttachApn(
            int32_t serial,
            const std::optional<data::DataProfileInfo>& dpInfo)
            override;

    ScopedAStatus allocatePduSessionId(int32_t serial) override;
    ScopedAStatus releasePduSessionId(int32_t serial, int32_t id) override;

    ScopedAStatus setupDataCall(
            int32_t serial, AccessNetwork accessNetwork,
            const data::DataProfileInfo& dataProfileInfo,
            bool roamingAllowed, data::DataRequestReason reason,
            const std::vector<data::LinkAddress>& addresses,
            const std::vector<std::string>& dnses, int32_t pduSessionId,
            const std::optional<data::SliceInfo>& sliceInfo,
            bool matchAllRuleAllowed) override;
    ScopedAStatus deactivateDataCall(
            int32_t serial, int32_t cid,
            data::DataRequestReason reason) override;
    ScopedAStatus getDataCallList(int32_t serial) override;

    ScopedAStatus startHandover(int32_t serial, int32_t callId) override;
    ScopedAStatus cancelHandover(int32_t serial, int32_t callId) override;

    ScopedAStatus startKeepalive(
            int32_t serial,
            const data::KeepaliveRequest& keepalive) override;
    ScopedAStatus stopKeepalive(int32_t serial, int32_t sessionHandle) override;

    ScopedAStatus setUserDataEnabled(int32_t serial, bool enabled) override;
    ScopedAStatus setUserDataRoamingEnabled(int32_t serial, bool enabled) override;

    ScopedAStatus notifyImsDataNetwork(int32_t serial, AccessNetwork accessNetwork,
                                       data::DataNetworkState dataNetworkState,
                                       data::TransportType physicalTransportType,
                                       int32_t physicalNetworkModemId) override;

    void atResponseSink(const AtResponsePtr& response);
    template <class IGNORE> void handleUnsolicited(const IGNORE&) {}

    ScopedAStatus responseAcknowledgement() override;
    ScopedAStatus setResponseFunctions(
            const std::shared_ptr<data::IRadioDataResponse>& radioDataResponse,
            const std::shared_ptr<data::IRadioDataIndication>& radioDataIndication) override;

private:
    int32_t allocateId();
    void releaseId(int32_t cid);
    RadioError validateKeepaliveRequest(const data::KeepaliveRequest& keepaliveReq) const;

    std::vector<data::SetupDataCallResult> getDataCalls() const;

    const std::shared_ptr<AtChannel> mAtChannel;
    AtChannel::Conversation mAtConversation;
    std::shared_ptr<data::IRadioDataResponse> mRadioDataResponse;
    std::shared_ptr<data::IRadioDataIndication> mRadioDataIndication;
    std::unordered_map<int32_t, data::SetupDataCallResult> mDataCalls;
    std::unordered_set<int32_t> mKeepAliveSessions;
    IdAllocator mIdAllocator;
    mutable std::mutex mMtx;
};

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
