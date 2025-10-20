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

#define FAILURE_DEBUG_PREFIX "Sap"

#include "Sap.h"
#include "debug.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {

Sap::Sap(std::shared_ptr<AtChannel> /*atChannel*/) {}

ScopedAStatus Sap::apduReq(const int32_t serial, sap::SapApduType /*type*/,
                           const std::vector<uint8_t>& /*command*/) {
    NOT_NULL(mSapCallback)->apduResponse(
        serial, FAILURE(sap::SapResultCode::GENERIC_FAILURE), {});
    return ScopedAStatus::ok();
}

ScopedAStatus Sap::connectReq(const int32_t serial, const int32_t /*maxMsgSize*/) {
    NOT_NULL(mSapCallback)->connectResponse(
        serial, FAILURE_V(sap::SapConnectRsp::CONNECT_FAILURE,
                          "%s", "NOT_SUPPORTED"),
        {});
    return ScopedAStatus::ok();
}

ScopedAStatus Sap::disconnectReq(const int32_t serial) {
    NOT_NULL(mSapCallback)->disconnectResponse(serial);
    return ScopedAStatus::ok();
}

ScopedAStatus Sap::powerReq(const int32_t serial, bool /*state*/) {
    NOT_NULL(mSapCallback)->powerResponse(
        serial, FAILURE(sap::SapResultCode::GENERIC_FAILURE));
    return ScopedAStatus::ok();
}

ScopedAStatus Sap::resetSimReq(const int32_t serial) {
    NOT_NULL(mSapCallback)->resetSimResponse(
        serial, FAILURE(sap::SapResultCode::GENERIC_FAILURE));
    return ScopedAStatus::ok();
}

ScopedAStatus Sap::setTransferProtocolReq(
        const int32_t serial, const sap::SapTransferProtocol /*transferProtocol*/) {
    NOT_NULL(mSapCallback)->transferProtocolResponse(
        serial, FAILURE(sap::SapResultCode::NOT_SUPPORTED));
    return ScopedAStatus::ok();
}

ScopedAStatus Sap::transferAtrReq(const int32_t serial) {
    NOT_NULL(mSapCallback)->transferAtrResponse(
        serial, FAILURE(sap::SapResultCode::GENERIC_FAILURE), {});
    return ScopedAStatus::ok();
}

ScopedAStatus Sap::transferCardReaderStatusReq(const int32_t serial) {
    NOT_NULL(mSapCallback)->transferCardReaderStatusResponse(
        serial, FAILURE(sap::SapResultCode::GENERIC_FAILURE), 0);
    return ScopedAStatus::ok();
}

void Sap::atResponseSink(const AtResponsePtr& /*response*/) {}

ScopedAStatus Sap::setCallback(const std::shared_ptr<sap::ISapCallback>& sapCallback) {
    mSapCallback = NOT_NULL(sapCallback);
    return ScopedAStatus::ok();
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
