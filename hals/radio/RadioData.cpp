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

#define FAILURE_DEBUG_PREFIX "RadioData"

#include <format>
#include <string_view>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "RadioData.h"

#include "debug.h"
#include "makeRadioResponseInfo.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
using data::DataProfileInfo;
using data::PdpProtocolType;
using data::SetupDataCallResult;

namespace {
constexpr char kInterfaceName[] = "eth0";

std::string_view getProtocolStr(const PdpProtocolType p) {
    using namespace std::literals;

    switch (p) {
    case PdpProtocolType::IP: return "IP"sv;
    case PdpProtocolType::IPV6: return "IPV6"sv;
    case PdpProtocolType::IPV4V6: return "IPV4V6"sv;
    case PdpProtocolType::PPP: return "PPP"sv;
    case PdpProtocolType::NON_IP: return "NON_IP"sv;
    case PdpProtocolType::UNSTRUCTURED: return "UNSTRUCTURED"sv;
    default: return {};
    }
}

std::string formatCGDCONT(const int cid,
                          const PdpProtocolType protocol,
                          const std::string_view apn) {
    const std::string_view protocolStr = getProtocolStr(protocol);
    if (protocolStr.empty()) {
        return FAILURE_V("", "Unexpected protocol: %s", toString(protocol).c_str());
    }

    if (apn.empty()) {
        return FAILURE_V("", "%s", "APN is empty");
    }

    return std::format("AT+CGDCONT={0:d},\"{1:s}\",\"{2:s}\",,0,0",
                       cid, protocolStr, apn);
}

bool setInterfaceState(const char* interfaceName, const bool on) {
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        return FAILURE_V(false, "Failed to open interface socket: %s (%d)",
                         strerror(errno), errno);
    }

    struct ifreq request;
    memset(&request, 0, sizeof(request));
    strncpy(request.ifr_name, interfaceName, sizeof(request.ifr_name));
    request.ifr_name[sizeof(request.ifr_name) - 1] = '\0';

    if (ioctl(sock, SIOCGIFFLAGS, &request)) {
        ::close(sock);
        return FAILURE_V(false, "Failed to get interface flags for %s: %s (%d)",
                         interfaceName, strerror(errno), errno);
    }

    if (((request.ifr_flags & IFF_UP) != 0) == on) {
        ::close(sock);
        return true;  // Interface already in desired state
    }

    request.ifr_flags ^= IFF_UP;
    if (ioctl(sock, SIOCSIFFLAGS, &request)) {
        ::close(sock);
        return FAILURE_V(false, "Failed to set interface flags for %s: %s (%d)",
                         interfaceName, strerror(errno), errno);
    }

    ::close(sock);
    return true;
}

bool setIpAddr(const char *addr, const int addrSize,
               const char* radioInterfaceName) {
    const int family = strchr(addr, ':') ? AF_INET6 : AF_INET;
    const int sock = socket(family, SOCK_DGRAM, 0);
    if (sock == -1) {
        return FAILURE_V(false, "Failed to open a %s socket: %s (%d)",
                         ((family == AF_INET) ? "INET" : "INET6"),
                         strerror(errno), errno);
    }

    struct ifreq req4;
    memset(&req4, 0, sizeof(req4));

    strncpy(req4.ifr_name, radioInterfaceName, sizeof(req4.ifr_name));
    req4.ifr_name[sizeof(req4.ifr_name) - 1] = '\0';

    if (family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&req4.ifr_addr;
        sin->sin_family = AF_INET;
        sin->sin_addr.s_addr = inet_addr(addr);
        if (ioctl(sock, SIOCSIFADDR, &req4) < 0) {
            ::close(sock);
            return FAILURE_V(false, "SIOCSIFADDR IPv4 failed: %s (%d)",
                             strerror(errno), errno);
        }

        sin->sin_addr.s_addr = htonl(0xFFFFFFFFu << (32 - (addrSize ? addrSize : 32)));
        if (ioctl(sock, SIOCSIFNETMASK, &req4) < 0) {
            ::close(sock);
            return FAILURE_V(false, "SIOCSIFNETMASK IPv4 failed: %s (%d)",
                             strerror(errno), errno);
        }
    } else {
        if (ioctl(sock, SIOCGIFINDEX, &req4) < 0) {
            ::close(sock);
            return FAILURE_V(false, "SIOCGIFINDEX IPv6 failed: %s (%d)",
                             strerror(errno), errno);
        }

        struct in6_ifreq req6 = {
            .ifr6_prefixlen = static_cast<__u32>(addrSize ? addrSize : 128),
            .ifr6_ifindex = req4.ifr_ifindex,
        };

        if (inet_pton(AF_INET6, addr, &req6.ifr6_addr) != 1) {
            ::close(sock);
            return FAILURE_V(false, "inet_pton(AF_INET6, '%s') failed: %s (%d)",
                             addr, strerror(errno), errno);
        }

        if (ioctl(sock, SIOCSIFADDR, &req6) < 0) {
            ::close(sock);
            return FAILURE_V(false, "SIOCSIFADDR failed: %s (%d)",
                             strerror(errno), errno);
        }
    }

    ::close(sock);
    return true;
}

} // namespace

RadioData::RadioData(std::shared_ptr<AtChannel> atChannel) : mAtChannel(std::move(atChannel)) {
}

ScopedAStatus RadioData::getSlicingConfig(const int32_t serial) {
    // matches reference-ril.c
    NOT_NULL(mRadioDataResponse)->getSlicingConfigResponse(
        makeRadioResponseInfo(serial), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::setDataAllowed(const int32_t serial,
                                        const bool /*allow*/) {
    // matches reference-ril.c
    NOT_NULL(mRadioDataResponse)->setDataAllowedResponse(
        makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::setDataProfile(const int32_t serial,
                                        const std::vector<DataProfileInfo>& /*profiles*/) {
    // matches reference-ril.c
    NOT_NULL(mRadioDataResponse)->setDataProfileResponse(
        makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::setDataThrottling(const int32_t serial,
                                           const data::DataThrottlingAction /*dataThrottlingAction*/,
                                           const int64_t /*completionDurationMillis*/) {
    // matches reference-ril.c
    NOT_NULL(mRadioDataResponse)->setDataThrottlingResponse(
        makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::setInitialAttachApn(const int32_t serial,
                                             const std::optional<DataProfileInfo>& /*maybeDpInfo*/) {
    // matches reference-ril.c
    NOT_NULL(mRadioDataResponse)->setInitialAttachApnResponse(
        makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::allocatePduSessionId(const int32_t serial) {
    NOT_NULL(mRadioDataResponse)->allocatePduSessionIdResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__), 0);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::releasePduSessionId(const int32_t serial,
                                             const int32_t /*id*/) {
    NOT_NULL(mRadioDataResponse)->releasePduSessionIdResponse(
        makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::setupDataCall(const int32_t serial,
                                       const AccessNetwork /*accessNetwork*/,
                                       const DataProfileInfo& dataProfileInfo,
                                       const bool /*roamingAllowed*/,
                                       const data::DataRequestReason /*reason*/,
                                       const std::vector<data::LinkAddress>& /*addresses*/,
                                       const std::vector<std::string>& /*dnses*/,
                                       const int32_t pduSessionId,
                                       const std::optional<data::SliceInfo>& /*sliceInfo*/,
                                       const bool /*matchAllRuleAllowed*/) {
    if (!setInterfaceState(kInterfaceName, true)) {
        NOT_NULL(mRadioDataResponse)->setupDataCallResponse(
                makeRadioResponseInfo(serial, FAILURE(RadioError::GENERIC_FAILURE)), {});
        return ScopedAStatus::ok();
    }

    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, dataProfileInfo, pduSessionId]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;
        using CGCONTRDP = AtResponse::CGCONTRDP;

        RadioError status;
        const int32_t cid = allocateId();

        std::string request = formatCGDCONT(cid, dataProfileInfo.protocol,
                                            dataProfileInfo.apn);
        if (request.empty()) {
            status = RadioError::INVALID_ARGUMENTS;

failed:     releaseId(cid);
            NOT_NULL(mRadioDataResponse)->setupDataCallResponse(
                    makeRadioResponseInfo(serial, FAILURE(status)), {});
            return status != RadioError::INTERNAL_ERR;
        }

        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<AtResponse::OK>() ||
                                      response.holds<CmeError>();
                            });
        if (!response) {
            status = FAILURE(RadioError::INTERNAL_ERR);
            goto failed;
        } else if (const CmeError* err = response->get_if<CmeError>()) {
            status = FAILURE_V(err->error, "%s",  toString(err->error).c_str());
            goto failed;
        } else if (!response->holds<AtResponse::OK>()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        SetupDataCallResult setupDataCallResult = {
            .suggestedRetryTime = -1,
            .cid = cid,
            .active = SetupDataCallResult::DATA_CONNECTION_STATUS_INACTIVE,
            .type = dataProfileInfo.protocol,
            .ifname = kInterfaceName,
            .mtuV4 = 1500,
            .mtuV6 = 1500,
            .handoverFailureMode = SetupDataCallResult::HANDOVER_FAILURE_MODE_LEGACY,
            .pduSessionId = pduSessionId,
        };

        request = std::format("AT+CGCONTRDP={0:d}", cid);
        response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CGCONTRDP>() ||
                                      response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
            goto failed;
        } else if (const CGCONTRDP* cgcontrdp = response->get_if<CGCONTRDP>()) {
            if (!setIpAddr(cgcontrdp->localAddr.c_str(),
                           cgcontrdp->localAddrSize,
                           setupDataCallResult.ifname.c_str())) {
                status = FAILURE(RadioError::GENERIC_FAILURE);
                goto failed;
            }

            const auto makeLinkAddress = [](const std::string_view address,
                                            const size_t addrSize) -> data::LinkAddress {
                return {
                    .address = std::format("{0:s}/{1:d}", address, addrSize),
                    .addressProperties = 0,
                    .deprecationTime = -1,
                    .expirationTime = -1,
                };
            };

            setupDataCallResult.addresses.push_back(
                makeLinkAddress(cgcontrdp->localAddr, cgcontrdp->localAddrSize));
            setupDataCallResult.gateways.push_back(cgcontrdp->gwAddr);
            setupDataCallResult.dnses.push_back(cgcontrdp->dns1);
            if (!cgcontrdp->dns2.empty()) {
                setupDataCallResult.dnses.push_back(cgcontrdp->dns2);
            }

            std::lock_guard<std::mutex> lock(mMtx);
            mDataCalls.insert({ cid, setupDataCallResult });
            status = RadioError::NONE;
        } else if (const CmeError* err = response->get_if<CmeError>()) {
            status = FAILURE_V(err->error, "%s",  toString(err->error).c_str());
            goto failed;
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioDataResponse)->setupDataCallResponse(
            makeRadioResponseInfo(serial), std::move(setupDataCallResult));
        return true;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::deactivateDataCall(
        const int32_t serial, const int32_t cid,
        const data::DataRequestReason /*reason*/) {
    bool removed;
    bool empty;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        const auto i = mDataCalls.find(cid);
        if (i != mDataCalls.end()) {
            mDataCalls.erase(i);
            mIdAllocator.put(cid);
            removed = true;
        } else {
            removed = false;
        }
        empty = mDataCalls.empty();
    }

    if (empty) {
        setInterfaceState(kInterfaceName, false);
    }

    if (removed) {
        NOT_NULL(mRadioDataResponse)->deactivateDataCallResponse(
            makeRadioResponseInfo(serial));
    } else {
        NOT_NULL(mRadioDataResponse)->deactivateDataCallResponse(
            makeRadioResponseInfo(serial, FAILURE(RadioError::INVALID_ARGUMENTS)));
    }

    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::getDataCallList(const int32_t serial) {
    NOT_NULL(mRadioDataResponse)->getDataCallListResponse(
        makeRadioResponseInfo(serial), getDataCalls());
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::startHandover(const int32_t serial,
                                       const int32_t /*callId*/) {
    NOT_NULL(mRadioDataResponse)->startHandoverResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::cancelHandover(const int32_t serial,
                                        const int32_t /*callId*/) {
    NOT_NULL(mRadioDataResponse)->cancelHandoverResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::startKeepalive(const int32_t serial,
                                        const data::KeepaliveRequest& /*keepalive*/) {
    const int32_t sessionHandle = allocateId();

    {
        std::lock_guard<std::mutex> lock(mMtx);
        mKeepAliveSessions.insert(sessionHandle);
    }

    using data::KeepaliveStatus;

    KeepaliveStatus keepaliveStatus = {
        .sessionHandle = sessionHandle,
        .code = KeepaliveStatus::CODE_ACTIVE,
    };

    NOT_NULL(mRadioDataResponse)->startKeepaliveResponse(
        makeRadioResponseInfo(serial), std::move(keepaliveStatus));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::stopKeepalive(const int32_t serial,
                                       const int32_t sessionHandle) {
    bool removed;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        removed = mKeepAliveSessions.erase(sessionHandle) > 0;
    }

    if (removed) {
        releaseId(sessionHandle);
    }

    NOT_NULL(mRadioDataResponse)->stopKeepaliveResponse(
        makeRadioResponseInfo(serial, removed ?
            RadioError::NONE : FAILURE(RadioError::INVALID_ARGUMENTS)));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioData::responseAcknowledgement() {
    return ScopedAStatus::ok();
}

void RadioData::atResponseSink(const AtResponsePtr& response) {
    if (!mAtConversation.send(response)) {
        response->visit([this](const auto& msg){ handleUnsolicited(msg); });
    }
}

ScopedAStatus RadioData::setResponseFunctions(
        const std::shared_ptr<data::IRadioDataResponse>& radioDataResponse,
        const std::shared_ptr<data::IRadioDataIndication>& radioDataIndication) {
    mRadioDataResponse = NOT_NULL(radioDataResponse);
    mRadioDataIndication = NOT_NULL(radioDataIndication);
    return ScopedAStatus::ok();
}

int32_t RadioData::allocateId() {
    std::lock_guard<std::mutex> lock(mMtx);
    return mIdAllocator.get();
}

void RadioData::releaseId(const int32_t cid) {
    std::lock_guard<std::mutex> lock(mMtx);
    mIdAllocator.put(cid);
}

std::vector<SetupDataCallResult> RadioData::getDataCalls() const {
    std::vector<SetupDataCallResult> dataCalls;

    std::lock_guard<std::mutex> lock(mMtx);
    for (const auto& kv : mDataCalls) {
        dataCalls.push_back(kv.second);
    }

    return dataCalls;
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
