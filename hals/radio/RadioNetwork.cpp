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

#define FAILURE_DEBUG_PREFIX "RadioNetwork"

#include <utils/SystemClock.h>

#include <aidl/android/hardware/radio/RadioConst.h>

#include "RadioNetwork.h"
#include "atCmds.h"
#include "debug.h"
#include "ratUtils.h"
#include "makeRadioResponseInfo.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
using network::AccessTechnologySpecificInfo;
using network::EutranBands;
using network::EutranRegistrationInfo;
using network::Cdma2000RegistrationInfo;
using network::CellConnectionStatus;
using network::CellIdentity;
using network::CellIdentityCdma;
using network::CellIdentityGsm;
using network::CellIdentityLte;
using network::CellIdentityNr;
using network::CellIdentityTdscdma;
using network::CellIdentityWcdma;
using network::CellInfo;
using network::CellInfoCdma;
using network::CellInfoGsm;
using network::CellInfoLte;
using network::CellInfoNr;
using network::CellInfoTdscdma;
using network::CellInfoWcdma;
using network::CellInfoRatSpecificInfo;
using network::NgranBands;
using network::OperatorInfo;
using network::RegStateResult;
using network::SignalStrength;

namespace {
// somehow RadioConst.h does not contain these values
constexpr int32_t kRadioConst_VALUE_UNAVAILABLE = 0x7FFFFFFF;  // b/382554555
constexpr uint8_t kRadioConst_VALUE_UNAVAILABLE_BYTE = 0xFFU;

CellIdentityCdma makeCellIdentityCdma(OperatorInfo operatorInfo) {
    CellIdentityCdma result = {
        .networkId = kRadioConst_VALUE_UNAVAILABLE,
        .systemId = kRadioConst_VALUE_UNAVAILABLE,
        .baseStationId = kRadioConst_VALUE_UNAVAILABLE,
        .longitude = kRadioConst_VALUE_UNAVAILABLE,
        .latitude = kRadioConst_VALUE_UNAVAILABLE,
    };

    result.operatorNames = std::move(operatorInfo);

    return result;
}

std::string getMcc(const OperatorInfo& operatorInfo) {
    return operatorInfo.operatorNumeric.substr(0, 3);
}

std::string getMnc(const OperatorInfo& operatorInfo) {
    return operatorInfo.operatorNumeric.substr(3);
}

CellIdentityGsm makeCellIdentityGsm(OperatorInfo operatorInfo,
                                    const int areaCode, const int cellId) {
    CellIdentityGsm result = {
        .mcc = getMcc(operatorInfo),
        .mnc = getMnc(operatorInfo),
        .lac = areaCode,
        .cid = cellId,
        .arfcn = 42,
        .bsic = 127, // kRadioConst_VALUE_UNAVAILABLE_BYTE, b/382555063
    };

    result.additionalPlmns.push_back(operatorInfo.operatorNumeric);
    result.operatorNames = std::move(operatorInfo);

    return result;
}

CellIdentityLte makeCellIdentityLte(OperatorInfo operatorInfo,
                                    const int areaCode, const int cellId) {
    CellIdentityLte result = {
        .mcc = getMcc(operatorInfo),
        .mnc = getMnc(operatorInfo),
        .ci = cellId,
        .pci = 0,
        .tac = areaCode,
        .earfcn = 103,
        .bandwidth = 10000,
    };

    result.additionalPlmns.push_back(operatorInfo.operatorNumeric);
    result.operatorNames = std::move(operatorInfo);
    result.bands.push_back(EutranBands::BAND_42);

    return result;
}

CellIdentityNr makeCellIdentityNr(OperatorInfo operatorInfo, const int areaCode) {
    std::string plmn = operatorInfo.operatorNumeric;

    CellIdentityNr result = {
        .mcc = getMcc(operatorInfo),
        .mnc = getMnc(operatorInfo),
        .nci = 100500,
        .pci = 555,
        .tac = areaCode,
        .nrarfcn = 9000,
    };

    result.additionalPlmns.push_back(operatorInfo.operatorNumeric);
    result.operatorNames = std::move(operatorInfo);
    result.bands.push_back(NgranBands::BAND_41);

    return result;
}

CellIdentityTdscdma makeCellIdentityTdscdma(OperatorInfo operatorInfo,
                                            const int areaCode, const int cellId) {
    CellIdentityTdscdma result = {
        .mcc = getMcc(operatorInfo),
        .mnc = getMnc(operatorInfo),
        .lac = areaCode,
        .cid = cellId,
        .cpid = kRadioConst_VALUE_UNAVAILABLE,
        .uarfcn = 777,
    };

    result.additionalPlmns.push_back(operatorInfo.operatorNumeric);
    result.operatorNames = std::move(operatorInfo);

    return result;
}

CellIdentityWcdma makeCellIdentityWcdma(OperatorInfo operatorInfo,
                                        const int areaCode, const int cellId) {
    CellIdentityWcdma result = {
        .mcc = getMcc(operatorInfo),
        .mnc = getMnc(operatorInfo),
        .lac = areaCode,
        .cid = cellId,
        .psc = 222,
        .uarfcn = 777,
    };

    result.additionalPlmns.push_back(operatorInfo.operatorNumeric);
    result.operatorNames = std::move(operatorInfo);

    return result;
}

OperatorInfo toOperatorInfo(AtResponse::COPS::OperatorInfo cops) {
    return {
        .alphaLong = std::move(cops.longName),
        .alphaShort = std::move(cops.shortName),
        .operatorNumeric = std::move(cops.numeric),
        .status = OperatorInfo::STATUS_CURRENT,
    };
}

using CellIdentityResult = std::pair<RadioError, CellIdentity>;

CellIdentityResult getCellIdentityImpl(OperatorInfo operatorInfo,
                                       const ratUtils::ModemTechnology mtech,
                                       const int areaCode, const int cellId,
                                       std::string* plmn) {
    using ratUtils::ModemTechnology;

    if (plmn) {
        *plmn = operatorInfo.operatorNumeric;
    }

    CellIdentity cellIdentity;

    switch (mtech) {
    case ModemTechnology::GSM:
        cellIdentity.set<CellIdentity::gsm>(makeCellIdentityGsm(std::move(operatorInfo),
                                                                areaCode, cellId));
        break;
    case ModemTechnology::WCDMA:
    case ModemTechnology::EVDO:
        cellIdentity.set<CellIdentity::wcdma>(makeCellIdentityWcdma(std::move(operatorInfo),
                                                                    areaCode, cellId));
        break;
    case ModemTechnology::CDMA:
        cellIdentity.set<CellIdentity::cdma>(makeCellIdentityCdma(std::move(operatorInfo)));
        break;
    case ModemTechnology::TDSCDMA:
        cellIdentity.set<CellIdentity::tdscdma>(makeCellIdentityTdscdma(std::move(operatorInfo),
                                                                        areaCode, cellId));
        break;
    case ModemTechnology::LTE:
        cellIdentity.set<CellIdentity::lte>(makeCellIdentityLte(std::move(operatorInfo),
                                                                areaCode, cellId));
        break;
    case ModemTechnology::NR:
        cellIdentity.set<CellIdentity::nr>(makeCellIdentityNr(std::move(operatorInfo), areaCode));
        break;
    default:
        return {FAILURE_V(RadioError::INTERNAL_ERR, "Unexpected radio technology: %u",
                          static_cast<unsigned>(mtech)), {}};
    };

    return {RadioError::NONE, std::move(cellIdentity)};
}

CellIdentityResult getCellIdentityImpl(const int areaCode, const int cellId, std::string* plmn,
                                       AtChannel::Conversation& atConversation,
                                       const AtChannel::RequestPipe requestPipe) {
    static const auto fail = [](RadioError e) -> CellIdentityResult { return {e, {}}; };

    using CmeError = AtResponse::CmeError;
    using COPS = AtResponse::COPS;
    using CTEC = AtResponse::CTEC;
    using ratUtils::ModemTechnology;

    OperatorInfo operatorInfo;
    AtResponsePtr response =
        atConversation(requestPipe, atCmds::getOperator,
                       [](const AtResponse& response) -> bool {
                           return response.holds<COPS>() || response.holds<CmeError>();
                       });
    if (!response || response->isParseError()) {
        return FAILURE(fail(RadioError::INTERNAL_ERR));
    } else if (const COPS* cops = response->get_if<COPS>()) {
        if ((cops->operators.size() == 1) && (cops->operators[0].isCurrent())) {
            operatorInfo = toOperatorInfo(cops->operators[0]);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
        }
    } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
        const RadioError status =
            (cmeError->error == RadioError::OPERATION_NOT_ALLOWED) ?
                RadioError::RADIO_NOT_AVAILABLE : cmeError->error;

        return fail(FAILURE_V(status, "%s", toString(status).c_str()));
    } else {
        response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
    }

    ModemTechnology mtech;
    response =
        atConversation(requestPipe, atCmds::getCurrentPreferredRadioTechs,
                       [](const AtResponse& response) -> bool {
                           return response.holds<CTEC>();
                       });
    if (!response || response->isParseError()) {
        return FAILURE(fail(RadioError::INTERNAL_ERR));
    } else if (const CTEC* ctec = response->get_if<CTEC>()) {
        mtech = ctec->getCurrentModemTechnology().value();
    } else {
        response->unexpected(FAILURE_DEBUG_PREFIX, __func__);
    }

    return getCellIdentityImpl(std::move(operatorInfo), mtech,
                               areaCode, cellId, plmn);
}

std::pair<RadioError, CellInfo> buildCellInfo(const bool registered,
                                              CellIdentity cellIdentity,
                                              SignalStrength signalStrength) {
    CellInfo cellInfo = {
        .registered = registered,
        .connectionStatus = registered ?
            CellConnectionStatus::PRIMARY_SERVING : CellConnectionStatus::NONE,
    };

    switch (cellIdentity.getTag()) {
    default:
        return {FAILURE_V(RadioError::INTERNAL_ERR, "%s",
                          "unexpected getTag"), {}};

    case CellIdentity::gsm: {
            CellInfoGsm cellInfoGsm = {
                .cellIdentityGsm = std::move(cellIdentity.get<CellIdentity::gsm>()),
                .signalStrengthGsm = std::move(signalStrength.gsm),
            };
            cellInfo.ratSpecificInfo.set<CellInfoRatSpecificInfo::gsm>(std::move(cellInfoGsm));
        }
        break;

    case CellIdentity::wcdma: {
            CellInfoWcdma cellInfoWcdma = {
                .cellIdentityWcdma = std::move(cellIdentity.get<CellIdentity::wcdma>()),
                .signalStrengthWcdma = std::move(signalStrength.wcdma),
            };
            cellInfo.ratSpecificInfo.set<CellInfoRatSpecificInfo::wcdma>(std::move(cellInfoWcdma));
        }
        break;

    case CellIdentity::tdscdma: {
            CellInfoTdscdma cellInfoTdscdma = {
                .cellIdentityTdscdma = std::move(cellIdentity.get<CellIdentity::tdscdma>()),
                .signalStrengthTdscdma = std::move(signalStrength.tdscdma),
            };
            cellInfo.ratSpecificInfo.set<CellInfoRatSpecificInfo::tdscdma>(std::move(cellInfoTdscdma));
        }
        break;

    case CellIdentity::cdma: {
            CellInfoCdma cellInfoCdma = {
                .cellIdentityCdma = std::move(cellIdentity.get<CellIdentity::cdma>()),
                .signalStrengthCdma = std::move(signalStrength.cdma),
            };
            cellInfo.ratSpecificInfo.set<CellInfoRatSpecificInfo::cdma>(std::move(cellInfoCdma));
        }
        break;


    case CellIdentity::lte: {
            CellInfoLte cellInfoLte = {
                .cellIdentityLte = std::move(cellIdentity.get<CellIdentity::lte>()),
                .signalStrengthLte = std::move(signalStrength.lte),
            };
            cellInfo.ratSpecificInfo.set<CellInfoRatSpecificInfo::lte>(std::move(cellInfoLte));
        }
        break;

    case CellIdentity::nr: {
            CellInfoNr cellInfoNr = {
                .cellIdentityNr = std::move(cellIdentity.get<CellIdentity::nr>()),
                .signalStrengthNr = std::move(signalStrength.nr),
            };
            cellInfo.ratSpecificInfo.set<CellInfoRatSpecificInfo::nr>(std::move(cellInfoNr));
        }
        break;
    }

    return {RadioError::NONE, std::move(cellInfo)};
}

void setAccessTechnologySpecificInfo(
        AccessTechnologySpecificInfo* accessTechnologySpecificInfo,
        const RadioTechnology rat) {
    switch (rat) {
    case RadioTechnology::LTE:
    case RadioTechnology::LTE_CA: {
            EutranRegistrationInfo eri = {
                .lteVopsInfo = {
                    .isVopsSupported = false,
                    .isEmcBearerSupported = false,
                },
            };

            accessTechnologySpecificInfo->set<
                AccessTechnologySpecificInfo::eutranInfo>(std::move(eri));
        }
        break;

    case RadioTechnology::NR: {
            EutranRegistrationInfo eri = {
                .nrIndicators = {
                    .isNrAvailable = true,
                    .isDcNrRestricted = false,
                    .isEndcAvailable = false,
                },
            };

            accessTechnologySpecificInfo->set<
                AccessTechnologySpecificInfo::eutranInfo>(std::move(eri));
        }
        break;

    case RadioTechnology::HSUPA:
    case RadioTechnology::HSDPA:
    case RadioTechnology::HSPA:
    case RadioTechnology::HSPAP:
    case RadioTechnology::UMTS:
    case RadioTechnology::IS95A:
    case RadioTechnology::IS95B:
    case RadioTechnology::ONE_X_RTT:
    case RadioTechnology::EVDO_0:
    case RadioTechnology::EVDO_A:
    case RadioTechnology::EVDO_B:
    case RadioTechnology::EHRPD:
    case RadioTechnology::TD_SCDMA: {
            Cdma2000RegistrationInfo cri = {
                .systemIsInPrl = Cdma2000RegistrationInfo::PRL_INDICATOR_IN_PRL,
            };

            accessTechnologySpecificInfo->set<
                AccessTechnologySpecificInfo::cdmaInfo>(std::move(cri));
        }
        break;

    default:
        break;
    }
}

}  // namespace

RadioNetwork::RadioNetwork(std::shared_ptr<AtChannel> atChannel) : mAtChannel(std::move(atChannel)) {
}

ScopedAStatus RadioNetwork::getAllowedNetworkTypesBitmap(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CTEC = AtResponse::CTEC;

        RadioError status = RadioError::NONE;
        uint32_t networkTypeBitmap = 0;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getCurrentPreferredRadioTechs,
                            [](const AtResponse& response) -> bool {
                                return response.holds<CTEC>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CTEC* ctec = response->get_if<CTEC>()) {
            networkTypeBitmap =
                ratUtils::supportedRadioTechBitmask(
                    ctec->getCurrentModemTechnology().value());
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioNetworkResponse)->getAllowedNetworkTypesBitmapResponse(
                makeRadioResponseInfo(serial, status), networkTypeBitmap);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getAvailableBandModes(const int32_t serial) {
    using network::RadioBandMode;

    NOT_NULL(mRadioNetworkResponse)->getAvailableBandModesResponse(
        makeRadioResponseInfo(serial), {
            RadioBandMode::BAND_MODE_UNSPECIFIED,
            RadioBandMode::BAND_MODE_EURO,
            RadioBandMode::BAND_MODE_USA,
            RadioBandMode::BAND_MODE_JPN,
            RadioBandMode::BAND_MODE_AUS,
            RadioBandMode::BAND_MODE_USA_2500M,
        });
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getAvailableNetworks(const int32_t serial) {
    NOT_NULL(mRadioNetworkResponse)->getAvailableNetworksResponse(
        makeRadioResponseInfoUnsupported(  // matches reference-ril.c
            serial, FAILURE_DEBUG_PREFIX, __func__), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getBarringInfo(const int32_t serial) {
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        int areaCode;
        int cellId;
        {
            std::lock_guard<std::mutex> lock(mMtx);
            areaCode = mCreg.areaCode;
            cellId = mCreg.cellId;
        }

        CellIdentityResult cellIdentityResult =
            getCellIdentityImpl(areaCode, cellId, nullptr, mAtConversation, requestPipe);
        if (cellIdentityResult.first == RadioError::NONE) {
            using network::BarringInfo;

            BarringInfo barringInfoCs = {
                .serviceType = BarringInfo::SERVICE_TYPE_CS_SERVICE,
                .barringType = BarringInfo::BARRING_TYPE_NONE,
            };

            BarringInfo barringInfoPs = {
                .serviceType = BarringInfo::SERVICE_TYPE_PS_SERVICE,
                .barringType = BarringInfo::BARRING_TYPE_NONE,
            };

            BarringInfo barringInfoCsVoice = {
                .serviceType = BarringInfo::SERVICE_TYPE_CS_VOICE,
                .barringType = BarringInfo::BARRING_TYPE_NONE,
            };

            BarringInfo barringInfoEmergency = {
                .serviceType = BarringInfo::SERVICE_TYPE_EMERGENCY,
                .barringType = BarringInfo::BARRING_TYPE_NONE,
            };

            NOT_NULL(mRadioNetworkResponse)->getBarringInfoResponse(
                    makeRadioResponseInfo(serial),
                    std::move(cellIdentityResult.second),
                    {
                        std::move(barringInfoCs),
                        std::move(barringInfoPs),
                        std::move(barringInfoCsVoice),
                        std::move(barringInfoEmergency),
                    });
            return true;
        } else {
            NOT_NULL(mRadioNetworkResponse)->getBarringInfoResponse(
                    makeRadioResponseInfo(serial,
                                          FAILURE_V(cellIdentityResult.first, "%s",
                                                    toString(cellIdentityResult.first).c_str())),
                    {}, {});
            return cellIdentityResult.first != RadioError::INTERNAL_ERR;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getCdmaRoamingPreference(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using WRMP = AtResponse::WRMP;
        using network::CdmaRoamingType;

        RadioError status = RadioError::NONE;
        CdmaRoamingType cdmaRoamingPreference = CdmaRoamingType::HOME_NETWORK;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getCdmaRoamingPreference,
                            [](const AtResponse& response) -> bool {
                               return response.holds<WRMP>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const WRMP* wrmp = response->get_if<WRMP>()) {
            cdmaRoamingPreference = wrmp->cdmaRoamingPreference;
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioNetworkResponse)->getCdmaRoamingPreferenceResponse(
            makeRadioResponseInfo(serial, status), cdmaRoamingPreference);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getCellInfoList(const int32_t serial) {
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        SignalStrength signalStrength;
        int areaCode;
        int cellId;
        bool registered;
        {
            std::lock_guard<std::mutex> lock(mMtx);
            signalStrength = mCsq.toSignalStrength();
            areaCode = mCreg.areaCode;
            cellId = mCreg.cellId;
            registered = (mCreg.state == network::RegState::REG_HOME);
        }

        RadioError status;
        CellIdentity cellIdentity;
        CellInfo cellInfo;

        std::tie(status, cellIdentity) =
            getCellIdentityImpl(areaCode, cellId, nullptr, mAtConversation, requestPipe);
        if (status == RadioError::NONE) {
            std::tie(status, cellInfo) = buildCellInfo(registered,
                                                       std::move(cellIdentity),
                                                       std::move(signalStrength));

            if (status == RadioError::NONE) {
                NOT_NULL(mRadioNetworkResponse)->getCellInfoListResponse(
                    makeRadioResponseInfo(serial), { std::move(cellInfo) });
                return true;
            }
        }

        NOT_NULL(mRadioNetworkResponse)->getCellInfoListResponse(
            makeRadioResponseInfo(serial, status), {});
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getDataRegistrationState(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CTEC = AtResponse::CTEC;

        RadioError status = RadioError::NONE;
        RegStateResult regStateResult;
        int areaCode;
        int cellId;

        {
            std::lock_guard<std::mutex> lock(mMtx);
            regStateResult.regState = mCreg.state;
            areaCode = mCreg.areaCode;
            cellId = mCreg.cellId;
        }

        std::tie(status, regStateResult.cellIdentity) =
            getCellIdentityImpl(areaCode, cellId, &regStateResult.registeredPlmn,
                                mAtConversation, requestPipe);
        if (status != RadioError::NONE) {
            goto failed;
        }

        {
            AtResponsePtr response =
                mAtConversation(requestPipe, atCmds::getCurrentPreferredRadioTechs,
                                [](const AtResponse& response) -> bool {
                                   return response.holds<CTEC>();
                                });
            if (!response || response->isParseError()) {
                status = FAILURE(RadioError::INTERNAL_ERR);
                goto failed;
            } else if (const CTEC* ctec = response->get_if<CTEC>()) {
                regStateResult.rat =
                    ratUtils::currentRadioTechnology(
                        ctec->getCurrentModemTechnology().value());
            } else {
                response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
            }
        }

        setAccessTechnologySpecificInfo(
            &regStateResult.accessTechnologySpecificInfo,
            regStateResult.rat);

        if (status == RadioError::NONE) {
            NOT_NULL(mRadioNetworkResponse)->getDataRegistrationStateResponse(
                makeRadioResponseInfo(serial), std::move(regStateResult));
            return true;
        } else {
failed:     NOT_NULL(mRadioNetworkResponse)->getDataRegistrationStateResponse(
                makeRadioResponseInfo(serial, status), {});
            return status != RadioError::INTERNAL_ERR;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getNetworkSelectionMode(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;
        using COPS = AtResponse::COPS;

        RadioError status = RadioError::NONE;
        bool manual = true;

        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getNetworkSelectionMode,
                            [](const AtResponse& response) -> bool {
                               return response.holds<COPS>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const COPS* cops = response->get_if<COPS>()) {
            manual = (cops->networkSelectionMode == COPS::NetworkSelectionMode::MANUAL);
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioNetworkResponse)->getNetworkSelectionModeResponse(
                makeRadioResponseInfo(serial, status), manual);
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getOperator(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;
        using COPS = AtResponse::COPS;

        RadioError status = RadioError::NONE;
        std::string longName;
        std::string shortName;
        std::string numeric;

        const AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getOperator,
                            [](const AtResponse& response) -> bool {
                               return response.holds<COPS>() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const COPS* cops = response->get_if<COPS>()) {
            if ((cops->operators.size() == 1) && (cops->operators[0].isCurrent())) {
                const COPS::OperatorInfo& current = cops->operators[0];

                longName = current.longName;
                shortName = current.shortName;
                numeric = current.numeric;
            } else {
                response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
            }
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = (cmeError->error == RadioError::OPERATION_NOT_ALLOWED) ?
                    RadioError::RADIO_NOT_AVAILABLE : cmeError->error;
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioNetworkResponse)->getOperatorResponse(
                makeRadioResponseInfo(serial, status),
                std::move(longName), std::move(shortName), std::move(numeric));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getSignalStrength(const int32_t serial) {
    network::SignalStrength signalStrength;

    RadioError status;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        signalStrength = mCsq.toSignalStrength();
        status = (mRadioState == modem::RadioState::ON) ?
            RadioError::NONE : FAILURE(RadioError::RADIO_NOT_AVAILABLE);
    }

    NOT_NULL(mRadioNetworkResponse)->getSignalStrengthResponse(
            makeRadioResponseInfo(serial, status), std::move(signalStrength));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getSystemSelectionChannels(const int32_t serial) {
    NOT_NULL(mRadioNetworkResponse)->getSystemSelectionChannelsResponse(
            makeRadioResponseInfoNOP(serial), {});
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getVoiceRadioTechnology(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CTEC = AtResponse::CTEC;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::getCurrentPreferredRadioTechs,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CTEC>();
                            });
        if (!response || response->isParseError()) {
            NOT_NULL(mRadioNetworkResponse)->getVoiceRadioTechnologyResponse(
                makeRadioResponseInfo(serial, FAILURE(RadioError::INTERNAL_ERR)), {});
            return false;
        } else if (const CTEC* ctec = response->get_if<CTEC>()) {
            NOT_NULL(mRadioNetworkResponse)->getVoiceRadioTechnologyResponse(
                makeRadioResponseInfo(serial),
                ratUtils::currentRadioTechnology(
                    ctec->getCurrentModemTechnology().value()));
            return true;
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getVoiceRegistrationState(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial](const AtChannel::RequestPipe requestPipe) -> bool {
        using CTEC = AtResponse::CTEC;

        RadioError status = RadioError::NONE;
        RegStateResult regStateResult;
        int areaCode;
        int cellId;

        {
            std::lock_guard<std::mutex> lock(mMtx);
            regStateResult.regState = mCreg.state;
            areaCode = mCreg.areaCode;
            cellId = mCreg.cellId;
        }

        CellIdentityResult cellIdentityResult =
            getCellIdentityImpl(areaCode, cellId, &regStateResult.registeredPlmn,
                                mAtConversation, requestPipe);
        if (cellIdentityResult.first == RadioError::NONE) {
            regStateResult.cellIdentity = std::move(cellIdentityResult.second);
        } else {
            status = cellIdentityResult.first;
            goto failed;
        }

        {
            AtResponsePtr response =
                mAtConversation(requestPipe, atCmds::getCurrentPreferredRadioTechs,
                                [](const AtResponse& response) -> bool {
                                   return response.holds<CTEC>();
                                });
            if (!response || response->isParseError()) {
                status = FAILURE(RadioError::INTERNAL_ERR);
                goto failed;
            } else if (const CTEC* ctec = response->get_if<CTEC>()) {
                regStateResult.rat =
                    ratUtils::currentRadioTechnology(
                        ctec->getCurrentModemTechnology().value());
            } else {
                response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
            }
        }

        setAccessTechnologySpecificInfo(
            &regStateResult.accessTechnologySpecificInfo,
            regStateResult.rat);

        if (status == RadioError::NONE) {
            NOT_NULL(mRadioNetworkResponse)->getVoiceRegistrationStateResponse(
                makeRadioResponseInfo(serial), std::move(regStateResult));
            return true;
        } else {
failed:     NOT_NULL(mRadioNetworkResponse)->getVoiceRegistrationStateResponse(
                makeRadioResponseInfo(serial, status), {});
            return status != RadioError::INTERNAL_ERR;
        }
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::isNrDualConnectivityEnabled(const int32_t serial) {
    bool enabled;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        enabled = mIsNrDualConnectivityEnabled;
    }

    NOT_NULL(mRadioNetworkResponse)->isNrDualConnectivityEnabledResponse(
            makeRadioResponseInfo(serial), enabled);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setAllowedNetworkTypesBitmap(const int32_t serial,
                                                         const int32_t networkTypeBitmap) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, networkTypeBitmap]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CTEC = AtResponse::CTEC;

        RadioError status = RadioError::NONE;

        const ratUtils::ModemTechnology currentTech =
            ratUtils::modemTechnologyFromRadioTechnologyBitmask(networkTypeBitmap);
        const uint32_t techBitmask =
            ratUtils::modemTechnologyBitmaskFromRadioTechnologyBitmask(networkTypeBitmap);

        const std::string request = std::format("AT+CTEC={0:d},\"{1:X}\"",
            (1 << static_cast<int>(currentTech)), techBitmask);
        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.holds<CTEC>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (!response->get_if<CTEC>()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioNetworkResponse)->setAllowedNetworkTypesBitmapResponse(
            makeRadioResponseInfo(serial, status));

        if (mRadioNetworkIndication) {
            mRadioNetworkIndication->voiceRadioTechChanged(
                RadioIndicationType::UNSOLICITED,
                ratUtils::currentRadioTechnology(currentTech));
        }
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setBandMode(const int32_t serial,
                                        const network::RadioBandMode /*mode*/) {
    NOT_NULL(mRadioNetworkResponse)->setBandModeResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setBarringPassword(const int32_t serial,
                                               const std::string& facility,
                                               const std::string& oldPassword,
                                               const std::string& newPassword) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, facility, oldPassword, newPassword]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;
        RadioError status = RadioError::NONE;

        const std::string request =
            std::format("AT+CPWD=\"{0:s}\",\"{1:s}\",\"{2:s}\"",
                        facility, oldPassword, newPassword);

        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioNetworkResponse)->setBarringPasswordResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setCdmaRoamingPreference(const int32_t serial,
                                                     const network::CdmaRoamingType type) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, type]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        RadioError status = RadioError::NONE;

        const std::string request =
            std::format("AT+WRMP={0:d}", static_cast<unsigned>(type));
        const AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioNetworkResponse)->setCdmaRoamingPreferenceResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setCellInfoListRate(const int32_t serial,
                                                const int32_t /*rate*/) {
    NOT_NULL(mRadioNetworkResponse)->setCellInfoListRateResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setIndicationFilter(const int32_t serial,
                                                const int32_t /*indicationFilter*/) {
    NOT_NULL(mRadioNetworkResponse)->setIndicationFilterResponse(
            makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setLinkCapacityReportingCriteria(const int32_t serial,
                                                             const int32_t /*hysteresisMs*/,
                                                             const int32_t /*hysteresisDlKbps*/,
                                                             const int32_t /*hysteresisUlKbps*/,
                                                             const std::vector<int32_t>& /*thresholdsDownlinkKbps*/,
                                                             const std::vector<int32_t>& /*thresholdsUplinkKbps*/,
                                                             const AccessNetwork /*accessNetwork*/) {
    NOT_NULL(mRadioNetworkResponse)->setLinkCapacityReportingCriteriaResponse(
            makeRadioResponseInfoNOP(serial));

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setLocationUpdates(const int32_t serial,
                                               const bool /*enable*/) {
    NOT_NULL(mRadioNetworkResponse)->setLocationUpdatesResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setNetworkSelectionModeAutomatic(const int32_t serial) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        RadioError status = RadioError::NONE;

        AtResponsePtr response =
            mAtConversation(requestPipe, atCmds::setNetworkSelectionModeAutomatic,
                            [](const AtResponse& response) -> bool {
                               return response.isOK();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioNetworkResponse)->setNetworkSelectionModeAutomaticResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setNetworkSelectionModeManual(const int32_t serial,
                                                          const std::string& operatorNumeric,
                                                          const radio::AccessNetwork ran) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, operatorNumeric, ran]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        using CmeError = AtResponse::CmeError;

        RadioError status = RadioError::NONE;

        std::string request;
        if (ran != radio::AccessNetwork::UNKNOWN) {
            request = std::format("AT+COPS={0:d},{1:d},\"{2:s}\",{3:d}",
                                  1, 2, operatorNumeric, static_cast<unsigned>(ran));
        } else {
            request = std::format("AT+COPS={0:d},{1:d},\"{2:s}\"",
                                  1, 2, operatorNumeric);
        }

        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK() || response.holds<CmeError>();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (response->isOK()) {
            // good
        } else if (const CmeError* cmeError = response->get_if<CmeError>()) {
            status = cmeError->getErrorAndLog(FAILURE_DEBUG_PREFIX, kFunc, __LINE__);
        } else {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioNetworkResponse)->setNetworkSelectionModeManualResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setNrDualConnectivityState(const int32_t serial,
                                                       const network::NrDualConnectivityState nrSt) {
    {
        std::lock_guard<std::mutex> lock(mMtx);
        mIsNrDualConnectivityEnabled =
            (nrSt == network::NrDualConnectivityState::ENABLE);
    }

    NOT_NULL(mRadioNetworkResponse)->setNrDualConnectivityStateResponse(
            makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setSignalStrengthReportingCriteria(const int32_t serial,
                                                               const std::vector<network::SignalThresholdInfo>& /*signalThresholdInfos*/) {
    NOT_NULL(mRadioNetworkResponse)->setSignalStrengthReportingCriteriaResponse(
            makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setSuppServiceNotifications(const int32_t serial,
                                                        const bool enable) {
    static const char* const kFunc = __func__;
    mAtChannel->queueRequester([this, serial, enable]
                               (const AtChannel::RequestPipe requestPipe) -> bool {
        RadioError status = RadioError::NONE;

        const int enableInt = enable ? 1 : 0;
        const std::string request = std::format("AT+CSSN={0:d},{1:d}",
            enableInt, enableInt);

        AtResponsePtr response =
            mAtConversation(requestPipe, request,
                            [](const AtResponse& response) -> bool {
                               return response.isOK();
                            });
        if (!response || response->isParseError()) {
            status = FAILURE(RadioError::INTERNAL_ERR);
        } else if (!response->isOK()) {
            response->unexpected(FAILURE_DEBUG_PREFIX, kFunc);
        }

        NOT_NULL(mRadioNetworkResponse)->setSuppServiceNotificationsResponse(
            makeRadioResponseInfo(serial, status));
        return status != RadioError::INTERNAL_ERR;
    });

    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setSystemSelectionChannels(const int32_t serial,
                                                       const bool /*specifyChannels*/,
                                                       const std::vector<network::RadioAccessSpecifier>& /*specifiers*/) {
    NOT_NULL(mRadioNetworkResponse)->setSystemSelectionChannelsResponse(
            makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::startNetworkScan(const int32_t serial,
                                             const network::NetworkScanRequest& /*request*/) {
    NOT_NULL(mRadioNetworkResponse)->startNetworkScanResponse(
        makeRadioResponseInfoNOP(serial));
    if (mRadioNetworkIndication) {
        mRadioNetworkIndication->networkScanResult(
            RadioIndicationType::UNSOLICITED, {});
    }
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::stopNetworkScan(const int32_t serial) {
    NOT_NULL(mRadioNetworkResponse)->stopNetworkScanResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::supplyNetworkDepersonalization(const int32_t serial,
                                                           const std::string& /*netPin*/) {
    NOT_NULL(mRadioNetworkResponse)->supplyNetworkDepersonalizationResponse(
        makeRadioResponseInfoNOP(serial), -1);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setUsageSetting(const int32_t serial,
                                            const network::UsageSetting /*usageSetting*/) {
    NOT_NULL(mRadioNetworkResponse)->setUsageSettingResponse(
            makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::getUsageSetting(const int32_t serial) {
    NOT_NULL(mRadioNetworkResponse)->getUsageSettingResponse(
            makeRadioResponseInfo(serial),
            network::UsageSetting::VOICE_CENTRIC);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setEmergencyMode(const int32_t serial,
                                             const network::EmergencyMode /*emergencyMode*/) {
    using network::Domain;
    using network::EmergencyRegResult;
    using network::RegState;

    EmergencyRegResult emergencyRegResult = {
        .accessNetwork = AccessNetwork::EUTRAN,
        .regState = RegState::REG_HOME,
        .emcDomain = static_cast<Domain>(
            static_cast<uint32_t>(Domain::CS) |
            static_cast<uint32_t>(Domain::PS)),
    };

    NOT_NULL(mRadioNetworkResponse)->setEmergencyModeResponse(
        makeRadioResponseInfo(serial), std::move(emergencyRegResult));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::triggerEmergencyNetworkScan(const int32_t serial,
                                                        const network::EmergencyNetworkScanTrigger& /*scanTrigger*/) {
    NOT_NULL(mRadioNetworkResponse)->triggerEmergencyNetworkScanResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::cancelEmergencyNetworkScan(const int32_t serial,
                                                       const bool /*resetScan*/) {
    NOT_NULL(mRadioNetworkResponse)->cancelEmergencyNetworkScanResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::exitEmergencyMode(const int32_t serial) {
    NOT_NULL(mRadioNetworkResponse)->exitEmergencyModeResponse(
        makeRadioResponseInfoNOP(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::isN1ModeEnabled(const int32_t serial) {
    bool enabled;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        enabled = mIsN1ModeEnabled;
    }

    NOT_NULL(mRadioNetworkResponse)->isN1ModeEnabledResponse(
            makeRadioResponseInfo(serial), enabled);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setN1ModeEnabled(const int32_t serial,
                                             const bool enable) {
    {
        std::lock_guard<std::mutex> lock(mMtx);
        mIsN1ModeEnabled = enable;
    }

    NOT_NULL(mRadioNetworkResponse)->setN1ModeEnabledResponse(
            makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setNullCipherAndIntegrityEnabled(const int32_t serial,
                                                             const bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mMtx);
        mNullCipherAndIntegrityEnabled = enabled;
    }

    NOT_NULL(mRadioNetworkResponse)->setNullCipherAndIntegrityEnabledResponse(
            makeRadioResponseInfo(serial, RadioError::NONE));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::isNullCipherAndIntegrityEnabled(const int32_t serial) {
    NOT_NULL(mRadioNetworkResponse)->isNullCipherAndIntegrityEnabledResponse(
            makeRadioResponseInfo(serial), mNullCipherAndIntegrityEnabled);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::isCellularIdentifierTransparencyEnabled(const int32_t serial) {
    bool enabled;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        enabled = mIsCellularIdentifierTransparencyEnabled;
    }

    NOT_NULL(mRadioNetworkResponse)->isCellularIdentifierTransparencyEnabledResponse(
            makeRadioResponseInfo(serial), enabled);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setCellularIdentifierTransparencyEnabled(const int32_t serial,
                                                                     const bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mMtx);
        mIsCellularIdentifierTransparencyEnabled = enabled;
    }

    NOT_NULL(mRadioNetworkResponse)->setCellularIdentifierTransparencyEnabledResponse(
            makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::setSecurityAlgorithmsUpdatedEnabled(const int32_t serial,
                                                                const bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mMtx);
        mSecurityAlgorithmsUpdatedEnabled = enabled;
    }

    NOT_NULL(mRadioNetworkResponse)->setSecurityAlgorithmsUpdatedEnabledResponse(
            makeRadioResponseInfo(serial));
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::isSecurityAlgorithmsUpdatedEnabled(const int32_t serial) {
    bool enabled;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        enabled = mSecurityAlgorithmsUpdatedEnabled;
    }

    NOT_NULL(mRadioNetworkResponse)->isSecurityAlgorithmsUpdatedEnabledResponse(
            makeRadioResponseInfo(serial), enabled);
    return ScopedAStatus::ok();
}

ScopedAStatus RadioNetwork::responseAcknowledgement() {
    return ScopedAStatus::ok();
}

void RadioNetwork::atResponseSink(const AtResponsePtr& response) {
    response->visit([this](const auto& msg){ handleUnsolicited(msg); });
    mAtConversation.send(response);
}

void RadioNetwork::handleUnsolicited(const AtResponse::CFUN& cfun) {
    bool changed;

    std::lock_guard<std::mutex> lock(mMtx);
    mRadioState = cfun.state;
    if (cfun.state == modem::RadioState::OFF) {
        changed = mCreg.state != network::RegState::NOT_REG_MT_NOT_SEARCHING_OP;
        mCreg.state = network::RegState::NOT_REG_MT_NOT_SEARCHING_OP;
        mCgreg.state = network::RegState::NOT_REG_MT_NOT_SEARCHING_OP;
    }

    if (changed && mRadioNetworkIndication) {
        mRadioNetworkIndication->networkStateChanged(RadioIndicationType::UNSOLICITED);
        mRadioNetworkIndication->imsNetworkStateChanged(RadioIndicationType::UNSOLICITED);
    }
}

void RadioNetwork::handleUnsolicited(const AtResponse::CREG& creg) {
    bool changed;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        changed = mCreg.state != creg.state;
        mCreg = creg;
    }

    if (changed && mRadioNetworkIndication) {
        mRadioNetworkIndication->networkStateChanged(RadioIndicationType::UNSOLICITED);
        mRadioNetworkIndication->imsNetworkStateChanged(RadioIndicationType::UNSOLICITED);
    }
}

void RadioNetwork::handleUnsolicited(const AtResponse::CGREG& cgreg) {
    std::lock_guard<std::mutex> lock(mMtx);
    mCgreg = cgreg;
}

void RadioNetwork::handleUnsolicited(const AtResponse::CSQ& csq) {
    SignalStrength signalStrength;
    std::vector<CellInfo> cellInfos;

    bool poweredOn;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        mCsq = csq;
        poweredOn = (mRadioState == modem::RadioState::ON);

        if (poweredOn) {
            signalStrength = csq.toSignalStrength();

            if (mCurrentOperator && mCurrentModemTech) {
                RadioError status;
                CellIdentity cellIdentity;
                std::tie(status, cellIdentity) =
                    getCellIdentityImpl(toOperatorInfo(mCurrentOperator.value()),
                                        mCurrentModemTech.value(),
                                        mCreg.areaCode, mCreg.cellId,
                                        nullptr);
                if (status == RadioError::NONE) {
                    const bool registered =
                        (mCreg.state == network::RegState::REG_HOME);

                    CellInfo cellinfo;
                    std::tie(status, cellinfo) =
                        buildCellInfo(registered, std::move(cellIdentity),
                                      signalStrength);
                    if (status == RadioError::NONE) {
                        cellInfos.push_back(std::move(cellinfo));
                    }
                }
            }
        }
    }

    if (poweredOn && mRadioNetworkIndication) {
        mRadioNetworkIndication->currentSignalStrength(
            RadioIndicationType::UNSOLICITED, std::move(signalStrength));

        if (!cellInfos.empty()) {
            mRadioNetworkIndication->cellInfoList(
                RadioIndicationType::UNSOLICITED, std::move(cellInfos));
        }
    }
}

void RadioNetwork::handleUnsolicited(const AtResponse::COPS& cops) {
    using COPS = AtResponse::COPS;

    if ((cops.operators.size() == 1) && (cops.operators[0].isCurrent())) {
        const COPS::OperatorInfo& current = cops.operators[0];

        std::lock_guard<std::mutex> lock(mMtx);
        mCurrentOperator = current;
    }
}

void RadioNetwork::handleUnsolicited(const AtResponse::CTEC& ctec) {
    auto currentModemTech = ctec.getCurrentModemTechnology();
    if (currentModemTech) {
        std::lock_guard<std::mutex> lock(mMtx);
        mCurrentModemTech = std::move(currentModemTech.value());
    }
}

void RadioNetwork::handleUnsolicited(const AtResponse::CGFPCCFG& cgfpccfg) {
    using network::CellConnectionStatus;
    using network::LinkCapacityEstimate;
    using network::PhysicalChannelConfig;

    bool registered;
    int cellId;
    int primaryBandwidth = 0;
    int secondaryBandwidth = 0;
    {
        std::lock_guard<std::mutex> lock(mMtx);
        registered = (mRadioState == modem::RadioState::ON) &&
            (mCreg.state == network::RegState::REG_HOME);
        cellId = mCreg.cellId;
        if (cgfpccfg.status == CellConnectionStatus::PRIMARY_SERVING) {
            mPrimaryBandwidth = cgfpccfg.bandwidth;
        } else {
            primaryBandwidth = mPrimaryBandwidth;
            secondaryBandwidth = cgfpccfg.bandwidth;
            mSecondaryBandwidth = cgfpccfg.bandwidth;
        }
    }

    if (registered && mRadioNetworkIndication) {
        {
            PhysicalChannelConfig physicalChannelConfig = {
                .status = cgfpccfg.status,
                .rat = ratUtils::currentRadioTechnology(cgfpccfg.mtech),
                .downlinkChannelNumber = 1,
                .uplinkChannelNumber = 2,
                .cellBandwidthDownlinkKhz = cgfpccfg.bandwidth,
                .cellBandwidthUplinkKhz = cgfpccfg.bandwidth / 2,
                .physicalCellId = cellId,
                // .band - TODO
            };

            physicalChannelConfig.contextIds.push_back(cgfpccfg.contextId);

            mRadioNetworkIndication->currentPhysicalChannelConfigs(
                RadioIndicationType::UNSOLICITED,
                { std::move(physicalChannelConfig) });
        }

        if (cgfpccfg.status == CellConnectionStatus::SECONDARY_SERVING) {
            LinkCapacityEstimate lce = {
                .downlinkCapacityKbps = primaryBandwidth * 3,
                .uplinkCapacityKbps = primaryBandwidth,
                .secondaryDownlinkCapacityKbps = secondaryBandwidth * 3,
                .secondaryUplinkCapacityKbps = secondaryBandwidth,
            };

            mRadioNetworkIndication->currentLinkCapacityEstimate(
                RadioIndicationType::UNSOLICITED, std::move(lce));
        }
    }
}

void RadioNetwork::handleUnsolicited(const AtResponse::CTZV& ctzv) {
    const int64_t now = ::android::elapsedRealtime();

    {
        std::lock_guard<std::mutex> lock(mMtx);
        mCtzv = ctzv;
        mCtzvTimestamp = now;
    }

    if (mRadioNetworkIndication) {
        mRadioNetworkIndication->nitzTimeReceived(
            RadioIndicationType::UNSOLICITED, ctzv.nitzString(), now, 0);
    }
}

ScopedAStatus RadioNetwork::setResponseFunctions(
        const std::shared_ptr<network::IRadioNetworkResponse>& radioNetworkResponse,
        const std::shared_ptr<network::IRadioNetworkIndication>& radioNetworkIndication) {
    mRadioNetworkResponse = NOT_NULL(radioNetworkResponse);
    mRadioNetworkIndication = NOT_NULL(radioNetworkIndication);

    AtResponse::CSQ csq;
    std::string nitz;
    int64_t nitzTs;
    bool poweredOn;

    {
        std::lock_guard<std::mutex> lock(mMtx);
        csq = mCsq;
        nitz = mCtzv.nitzString();
        nitzTs = mCtzvTimestamp;
        poweredOn = (mRadioState == modem::RadioState::ON);
    }

    if (poweredOn) {
        radioNetworkIndication->networkStateChanged(RadioIndicationType::UNSOLICITED);

        radioNetworkIndication->currentSignalStrength(
            RadioIndicationType::UNSOLICITED, csq.toSignalStrength());

        radioNetworkIndication->nitzTimeReceived(
            RadioIndicationType::UNSOLICITED, std::move(nitz), nitzTs, 0);
    }

    return ScopedAStatus::ok();
}

/************************* deprecated *************************/
ScopedAStatus RadioNetwork::getImsRegistrationState(const int32_t serial) {
    NOT_NULL(mRadioNetworkResponse)->getImsRegistrationStateResponse(
        makeRadioResponseInfoDeprecated(serial), {}, {});
    return ScopedAStatus::ok();
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
