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

#include <string_view>

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
namespace atCmds {
using namespace std::literals;

static constexpr std::string_view kCmeErrorOperationNotAllowed = "3"sv;
static constexpr std::string_view kCmeErrorOperationNotSupported = "4"sv;
static constexpr std::string_view kCmeErrorSimNotInserted = "10"sv;
static constexpr std::string_view kCmeErrorSimPinRequired = "11"sv;
static constexpr std::string_view kCmeErrorSimPukRequired = "12"sv;
static constexpr std::string_view kCmeErrorSimBusy = "14"sv;
static constexpr std::string_view kCmeErrorIncorrectPassword = "16"sv;
static constexpr std::string_view kCmeErrorMemoryFull = "20"sv;
static constexpr std::string_view kCmeErrorInvalidIndex = "21"sv;
static constexpr std::string_view kCmeErrorNotFound = "22"sv;
static constexpr std::string_view kCmeErrorInvalidCharactersInTextString = "27"sv;
static constexpr std::string_view kCmeErrorNoNetworkService = "30"sv;
static constexpr std::string_view kCmeErrorNetworkNotAllowedEmergencyCallsOnly = "32"sv;
static constexpr std::string_view kCmeErrorInCorrectParameters = "50"sv;
static constexpr std::string_view kCmeErrorNetworkNotAttachedDueToMTFunctionalRestrictions = "53"sv;
static constexpr std::string_view kCmeErrorFixedDialNumberOnlyAllowed = "56"sv;

static constexpr std::string_view kCmsErrorOperationNotAllowed = "302";
static constexpr std::string_view kCmsErrorOperationNotSupported = "303";
static constexpr std::string_view kCmsErrorInvalidPDUModeParam = "304";
static constexpr std::string_view kCmsErrorSCAddressUnknown = "304";

constexpr int kClckUnlock = 0;
constexpr int kClckLock = 1;
constexpr int kClckQuery = 2;

static constexpr std::string_view getModemPowerState =
    "AT+CFUN?"sv;

static constexpr std::string_view getSupportedRadioTechs =
    "AT+CTEC=?"sv;

static constexpr std::string_view getCurrentPreferredRadioTechs =
    "AT+CTEC?"sv;

static constexpr std::string_view getSimCardStatus =
    "AT+CPIN?"sv;

static constexpr std::string_view reportStkServiceRunning =
    "AT+CUSATD?"sv;

static constexpr std::string_view getICCID = "AT+CICCID"sv;

static constexpr std::string_view getIMEI = "AT+CGSN=2"sv;

static constexpr std::string_view getIMSI = "AT+CIMI"sv;

static constexpr std::string_view getSignalStrength =
    "AT+CSQ"sv;

static constexpr std::string_view getNetworkSelectionMode =
    "AT+COPS?"sv;

static constexpr std::string_view getAvailableNetworks =
    "AT+COPS=?"sv;

static constexpr std::string_view getOperator =
    "AT+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?"sv;

static constexpr std::string_view setNetworkSelectionModeAutomatic =
    "AT+COPS=0"sv;

static constexpr std::string_view getCdmaRoamingPreference =
    "AT+WRMP?"sv;

static constexpr std::string_view getVoiceRegistrationState =
    "AT+CREG?"sv;

static constexpr std::string_view getDataRegistrationState =
    "AT+CEREG?"sv;

static constexpr std::string_view getCdmaSubscriptionSource =
    "AT+CCSS?"sv;

static constexpr std::string_view getCurrentCalls = "AT+CLCC"sv;

static constexpr std::string_view acceptCall = "ATA"sv;
static constexpr std::string_view rejectCall = "ATH"sv;
static constexpr std::string_view hangupWaiting = "AT+CHLD=0"sv;
static constexpr std::string_view hangupForeground = "AT+CHLD=1"sv;
static constexpr std::string_view switchWaiting = "AT+CHLD=2"sv;
static constexpr std::string_view conference = "AT+CHLD=3"sv;

static constexpr std::string_view cancelUssd = "AT+CUSD=2"sv;

static constexpr std::string_view getClip = "AT+CLIP?"sv;
static constexpr std::string_view getClir = "AT+CLIR?"sv;
static constexpr std::string_view getMute = "AT+CMUT?"sv;

static constexpr std::string_view exitEmergencyMode = "AT+WSOS=0"sv;

static constexpr std::string_view getSmscAddress = "AT+CSCA?"sv;

static constexpr std::string_view getBroadcastConfig = "AT+CSCB?"sv;

}  // namespace atCmds
}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl

