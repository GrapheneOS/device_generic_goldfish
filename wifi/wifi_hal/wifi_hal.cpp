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

#include <wifi_hal.h>

#include "info.h"
#include "interface.h"

#include <memory>

template<typename>
struct NotSupportedFunction;

template<typename R, typename... Args>
struct NotSupportedFunction<R (*)(Args...)> {
    static constexpr R invoke(Args...) { return WIFI_ERROR_NOT_SUPPORTED; }
};

template<typename... Args>
struct NotSupportedFunction<void (*)(Args...)> {
    static constexpr void invoke(Args...) { }
};

template<typename T>
void notSupported(T& val) {
    val = &NotSupportedFunction<T>::invoke;
}

Info* asInfo(wifi_handle h) {
    return reinterpret_cast<Info*>(h);
}

Interface* asInterface(wifi_interface_handle h) {
    return reinterpret_cast<Interface*>(h);
}

wifi_error wifi_initialize(wifi_handle* handle) {
    if (handle == nullptr) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    // Do this using a unique_ptr to ensure someone is always owning and
    // destroying the pointer.
    auto info = std::make_unique<Info>();
    if (!info->init()) {
        return WIFI_ERROR_UNKNOWN;
    }
    *handle = reinterpret_cast<wifi_handle>(info.release());

    return WIFI_SUCCESS;
}

void wifi_cleanup(wifi_handle handle, wifi_cleaned_up_handler handler) {
    if (handle == nullptr) {
        return;
    }

    auto info = asInfo(handle);
    info->stop();
    delete info;

    // Notify callback that clean-up is complete
    handler(handle);
}

void wifi_event_loop(wifi_handle handle) {
    if (handle == nullptr) {
        return;
    }

    asInfo(handle)->eventLoop();
}

wifi_error wifi_get_supported_feature_set(wifi_interface_handle handle,
                                          feature_set* set) {
    if (handle == nullptr) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    return asInterface(handle)->getSupportedFeatureSet(set);
}

wifi_error wifi_get_ifaces(wifi_handle handle,
                           int* num,
                           wifi_interface_handle** interfaces) {
    if (handle == nullptr) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    return asInfo(handle)->getInterfaces(num, interfaces);
}

wifi_error wifi_get_iface_name(wifi_interface_handle handle,
                               char* name,
                               size_t size) {
    if (handle == nullptr || (name == nullptr && size > 0)) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    return asInterface(handle)->getName(name, size);
}

wifi_error wifi_get_link_stats(wifi_request_id id,
                               wifi_interface_handle handle,
                               wifi_stats_result_handler handler) {
    if (handle == nullptr) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    return asInterface(handle)->getLinkStats(id, handler);
}

wifi_error wifi_set_link_stats(wifi_interface_handle handle,
                               wifi_link_layer_params params) {
    if (handle == nullptr) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    return asInterface(handle)->setLinkStats(params);
}

wifi_error wifi_set_alert_handler(wifi_request_id id,
                                  wifi_interface_handle handle,
                                  wifi_alert_handler handler) {
    if (handle == nullptr) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    return asInterface(handle)->setAlertHandler(id, handler);
}

wifi_error wifi_reset_alert_handler(wifi_request_id id,
                                    wifi_interface_handle handle) {
    if (handle == nullptr) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    return asInterface(handle)->resetAlertHandler(id);
}

wifi_error wifi_get_firmware_version(wifi_interface_handle handle,
                                     char* buffer,
                                     int buffer_size) {
    if (handle == nullptr) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    return asInterface(handle)->getFirmwareVersion(buffer, buffer_size);
}

wifi_error wifi_get_driver_version(wifi_interface_handle handle,
                                   char* buffer,
                                   int buffer_size) {
    if (handle == nullptr) {
        return WIFI_ERROR_INVALID_ARGS;
    }

    return asInterface(handle)->getDriverVersion(buffer, buffer_size);
}

wifi_error init_wifi_vendor_hal_func_table(wifi_hal_fn* fn)
{
    if (fn == NULL) {
        return WIFI_ERROR_UNKNOWN;
    }
    fn->wifi_initialize = wifi_initialize;
    fn->wifi_cleanup = wifi_cleanup;
    fn->wifi_event_loop = wifi_event_loop;
    fn->wifi_get_supported_feature_set = wifi_get_supported_feature_set;

    fn->wifi_get_ifaces = wifi_get_ifaces;
    fn->wifi_get_iface_name = wifi_get_iface_name;
    fn->wifi_get_link_stats = wifi_get_link_stats;
    fn->wifi_set_link_stats = wifi_set_link_stats;

    fn->wifi_set_alert_handler = wifi_set_alert_handler;
    fn->wifi_reset_alert_handler = wifi_reset_alert_handler;
    fn->wifi_get_firmware_version = wifi_get_firmware_version;
    fn->wifi_get_driver_version = wifi_get_driver_version;

    // These function will either return WIFI_ERROR_NOT_SUPPORTED or do nothing
    notSupported(fn->wifi_set_scanning_mac_oui);
    notSupported(fn->wifi_set_nodfs_flag);
    notSupported(fn->wifi_get_concurrency_matrix);
    notSupported(fn->wifi_start_gscan);
    notSupported(fn->wifi_stop_gscan);
    notSupported(fn->wifi_get_cached_gscan_results);
    notSupported(fn->wifi_set_bssid_hotlist);
    notSupported(fn->wifi_reset_bssid_hotlist);
    notSupported(fn->wifi_set_significant_change_handler);
    notSupported(fn->wifi_reset_significant_change_handler);
    notSupported(fn->wifi_get_gscan_capabilities);
    notSupported(fn->wifi_clear_link_stats);
    notSupported(fn->wifi_get_valid_channels);
    notSupported(fn->wifi_rtt_range_request);
    notSupported(fn->wifi_rtt_range_cancel);
    notSupported(fn->wifi_get_rtt_capabilities);
    notSupported(fn->wifi_rtt_get_responder_info);
    notSupported(fn->wifi_enable_responder);
    notSupported(fn->wifi_disable_responder);
    notSupported(fn->wifi_start_logging);
    notSupported(fn->wifi_set_epno_list);
    notSupported(fn->wifi_reset_epno_list);
    notSupported(fn->wifi_set_country_code);
    notSupported(fn->wifi_get_firmware_memory_dump);
    notSupported(fn->wifi_set_log_handler);
    notSupported(fn->wifi_reset_log_handler);
    notSupported(fn->wifi_get_ring_buffers_status);
    notSupported(fn->wifi_get_logger_supported_feature_set);
    notSupported(fn->wifi_get_ring_data);
    notSupported(fn->wifi_start_rssi_monitoring);
    notSupported(fn->wifi_stop_rssi_monitoring);
    notSupported(fn->wifi_configure_nd_offload);
    notSupported(fn->wifi_start_sending_offloaded_packet);
    notSupported(fn->wifi_stop_sending_offloaded_packet);
    notSupported(fn->wifi_start_pkt_fate_monitoring);
    notSupported(fn->wifi_get_tx_pkt_fates);
    notSupported(fn->wifi_get_rx_pkt_fates);
    notSupported(fn->wifi_get_packet_filter_capabilities);
    notSupported(fn->wifi_set_packet_filter);

    return WIFI_SUCCESS;
}

