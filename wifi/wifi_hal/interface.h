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

#pragma once

#include <wifi_hal.h>

#include <stdint.h>
#include <string>

class Netlink;
class NetlinkMessage;

class Interface {
public:
    Interface(Netlink& netlink, const char* name);
    Interface(Interface&& other);

    bool init();

    wifi_error getSupportedFeatureSet(feature_set* set);
    wifi_error getName(char* name, size_t size);
    wifi_error getLinkStats(wifi_request_id requestId,
                            wifi_stats_result_handler handler);
    wifi_error setLinkStats(wifi_link_layer_params params);
    wifi_error setAlertHandler(wifi_request_id id, wifi_alert_handler handler);
    wifi_error resetAlertHandler(wifi_request_id id);
    wifi_error getFirmwareVersion(char* buffer, size_t size);
    wifi_error getDriverVersion(char* buffer, size_t size);
private:
    Interface(const Interface&) = delete;
    Interface& operator=(const Interface&) = delete;

    void onLinkStatsReply(wifi_request_id requestId,
                          wifi_stats_result_handler handler,
                          const NetlinkMessage& reply);

    Netlink& mNetlink;
    std::string mName;
    uint32_t mInterfaceIndex;
};

