/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "interface.h"

#include "log.h"
#include "netlink.h"
#include "netlinkmessage.h"

#include <linux/rtnetlink.h>

// Provide some arbitrary firmware and driver versions for now
static const char kFirmwareVersion[] = "1.0";
static const char kDriverVersion[] = "1.0";

Interface::Interface(Netlink& netlink, const char* name)
    : mNetlink(netlink)
    , mName(name)
    , mInterfaceIndex(0) {
}

Interface::Interface(Interface&& other)
    : mNetlink(other.mNetlink)
    , mName(std::move(other.mName))
    , mInterfaceIndex(other.mInterfaceIndex) {
}

bool Interface::init() {
    mInterfaceIndex = if_nametoindex(mName.c_str());
    if (mInterfaceIndex == 0) {
        ALOGE("Unable to get interface index for %s", mName.c_str());
        return false;
    }
    return true;
}

wifi_error Interface::getSupportedFeatureSet(feature_set* set) {
    if (set == nullptr) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    *set = WIFI_FEATURE_LINK_LAYER_STATS;
    return WIFI_SUCCESS;
}

wifi_error Interface::getName(char* name, size_t size) {
    if (size < mName.size() + 1) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    strlcpy(name, mName.c_str(), size);
    return WIFI_SUCCESS;
}

wifi_error Interface::getLinkStats(wifi_request_id requestId,
                                   wifi_stats_result_handler handler) {
    NetlinkMessage message(RTM_GETLINK, mNetlink.getSequenceNumber());

    ifinfomsg* info = message.payload<ifinfomsg>();
    info->ifi_family = AF_UNSPEC;
    info->ifi_type = 1;
    info->ifi_index = mInterfaceIndex;
    info->ifi_flags = 0;
    info->ifi_change = 0xFFFFFFFF;

    bool success = mNetlink.sendMessage(message,
                                        std::bind(&Interface::onLinkStatsReply,
                                                  this,
                                                  requestId,
                                                  handler,
                                                  std::placeholders::_1));
    return success ? WIFI_SUCCESS : WIFI_ERROR_UNKNOWN;
}

wifi_error Interface::setLinkStats(wifi_link_layer_params /*params*/) {
    return WIFI_SUCCESS;
}

wifi_error Interface::setAlertHandler(wifi_request_id /*id*/,
                                         wifi_alert_handler /*handler*/) {
    return WIFI_SUCCESS;
}

wifi_error Interface::resetAlertHandler(wifi_request_id /*id*/) {
    return WIFI_SUCCESS;
}

wifi_error Interface::getFirmwareVersion(char* buffer, size_t size) {
    if (size < sizeof(kFirmwareVersion)) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    strcpy(buffer, kFirmwareVersion);
    return WIFI_SUCCESS;
}

wifi_error Interface::getDriverVersion(char* buffer, size_t size) {
    if (size < sizeof(kDriverVersion)) {
        return WIFI_ERROR_INVALID_ARGS;
    }
    strcpy(buffer, kDriverVersion);
    return WIFI_SUCCESS;
}

void Interface::onLinkStatsReply(wifi_request_id requestId,
                                 wifi_stats_result_handler handler,
                                 const NetlinkMessage& message) {
    if (message.size() < sizeof(nlmsghdr) + sizeof(ifinfomsg)) {
        ALOGE("Invalid link stats response, too small");
        return;
    }
    if (message.type() != RTM_NEWLINK) {
        ALOGE("Recieved invalid link stats reply type: %u",
              static_cast<unsigned int>(message.type()));
        return;
    }

    int numRadios = 1;
    wifi_radio_stat radioStats;
    memset(&radioStats, 0, sizeof(radioStats));

    wifi_iface_stat ifStats;
    memset(&ifStats, 0, sizeof(ifStats));
    ifStats.iface = reinterpret_cast<wifi_interface_handle>(this);

    rtnl_link_stats64 netlinkStats64;
    rtnl_link_stats netlinkStats;
    if (message.getAttribute(IFLA_STATS64, &netlinkStats64)) {
        ifStats.ac[WIFI_AC_BE].tx_mpdu = netlinkStats64.tx_packets;
        ifStats.ac[WIFI_AC_BE].rx_mpdu = netlinkStats64.rx_packets;
    } else if (message.getAttribute(IFLA_STATS, &netlinkStats)) {
        ifStats.ac[WIFI_AC_BE].tx_mpdu = netlinkStats.tx_packets;
        ifStats.ac[WIFI_AC_BE].rx_mpdu = netlinkStats.rx_packets;
    } else {
        return;
    }

    handler.on_link_stats_results(requestId, &ifStats, numRadios, &radioStats);
}
