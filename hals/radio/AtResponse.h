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
#include <string>
#include <source_location>
#include <string_view>
#include <variant>

#include <aidl/android/hardware/radio/RadioError.h>
#include <aidl/android/hardware/radio/modem/RadioState.h>
#include <aidl/android/hardware/radio/network/CellConnectionStatus.h>
#include <aidl/android/hardware/radio/network/CdmaRoamingType.h>
#include <aidl/android/hardware/radio/network/RegState.h>
#include <aidl/android/hardware/radio/network/SignalStrength.h>
#include <aidl/android/hardware/radio/RadioTechnology.h>
#include <aidl/android/hardware/radio/sim/CdmaSubscriptionSource.h>
#include <aidl/android/hardware/radio/voice/Call.h>
#include <aidl/android/hardware/radio/voice/CallForwardInfo.h>
#include <aidl/android/hardware/radio/voice/ClipStatus.h>

#include "ratUtils.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {

struct AtResponse;
using AtResponsePtr = std::shared_ptr<const AtResponse>;

struct AtResponse {
    using ParseResult = std::pair<int, AtResponsePtr>;

    struct OK {};
    struct ERROR {};
    struct RING {};
    struct SmsPrompt {};

    struct CmeError {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CME ERROR"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        RadioError getErrorAndLog(const char* klass,
                                  const char* func, int line) const;

        RadioError error;
    };

    struct CmsError {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CMS ERROR"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::string message;
    };

    struct ParseError {
        std::string_view cmd;
    };

    struct CPIN {
        enum class State {
            ABSENT, NOT_READY, READY, PIN, PUK
        };

        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CPIN"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        State state;
    };

    struct CPINR {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CPINR"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        int remainingRetryTimes = -1;
        int maxRetryTimes = -1;
    };

    struct CRSM {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CRSM"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::string response;
        int sw1 = -1;
        int sw2 = -1;
    };

    struct CFUN {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CFUN"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        modem::RadioState state;
    };

    struct CIMI {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CIMI"sv;
        }
    };

    struct CREG {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CREG"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        int areaCode = -1;
        int cellId = -1;
        int networkType = -1;
        network::RegState state = network::RegState::NOT_REG_MT_NOT_SEARCHING_OP;
    };

    struct CGREG {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CGREG"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        int areaCode = -1;
        int cellId = -1;
        int networkType = -1;
        network::RegState state = network::RegState::NOT_REG_MT_NOT_SEARCHING_OP;
    };

    struct CEREG {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CEREG"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        int areaCode = -1;
        int cellId = -1;
        int networkType = -1;
        network::RegState state = network::RegState::NOT_REG_MT_NOT_SEARCHING_OP;
    };

    struct CTEC {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CTEC"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::optional<ratUtils::ModemTechnology> getCurrentModemTechnology() const;
        bool isDONE() const;

        std::vector<std::string> values;
    };

    struct COPS {
        enum class NetworkSelectionMode {
            AUTOMATIC, MANUAL, DEREGISTER, SET_FORMAT, MANUAL_AUTOMATIC
        };

        static constexpr std::string_view id() {
            using namespace std::literals;
            return "COPS"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        struct OperatorInfo {
            enum class State {
                UNKNOWN, AVAILABLE, CURRENT, FORBIDDEN
            };

            bool isCurrent() const {
                return state == State::CURRENT;
            }

            std::string mcc() const {
                return numeric.substr(0, 3);
            }

            std::string mnc() const {
                return numeric.substr(3);
            }

            State state = State::UNKNOWN;
            std::string longName;
            std::string shortName;
            std::string numeric;
        };

        std::vector<OperatorInfo> operators;
        std::string numeric;
        NetworkSelectionMode networkSelectionMode = NetworkSelectionMode::AUTOMATIC;
    };

    struct WRMP {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "WRMP"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        network::CdmaRoamingType cdmaRoamingPreference;
    };

    struct CCSS {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CCSS"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        sim::CdmaSubscriptionSource source;
    };

    struct CSQ {
        static constexpr int32_t kUnknown = INT32_MAX;

        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CSQ"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        network::SignalStrength toSignalStrength() const;

        int32_t gsm_signalStrength = kUnknown;
        int32_t gsm_bitErrorRate = kUnknown;
        int32_t gsm_timingAdvance = kUnknown;
        int32_t cdma_dbm = kUnknown;
        int32_t cdma_ecio = kUnknown;
        int32_t evdo_dbm = kUnknown;
        int32_t evdo_ecio = kUnknown;
        int32_t evdo_signalNoiseRatio = kUnknown;
        int32_t lte_signalStrength = kUnknown;
        int32_t lte_rsrp = kUnknown;
        int32_t lte_rsrq = kUnknown;
        int32_t lte_rssnr = kUnknown;
        int32_t lte_cqi = kUnknown;
        int32_t lte_timingAdvance = kUnknown;
        int32_t lte_cqiTableIndex = kUnknown;
        int32_t tdscdma_signalStrength = kUnknown;
        int32_t tdscdma_bitErrorRate = kUnknown;
        int32_t tdscdma_rscp = kUnknown;
        int32_t wcdma_signalStrength = kUnknown;
        int32_t wcdma_bitErrorRate = kUnknown;
        int32_t wcdma_rscp = kUnknown;
        int32_t wcdma_ecno = kUnknown;
        int32_t nr_ssRsrp = kUnknown;
        int32_t nr_ssRsrq = kUnknown;
        int32_t nr_ssSinr = kUnknown;
        int32_t nr_csiRsrp = kUnknown;
        int32_t nr_csiRsrq = kUnknown;
        int32_t nr_csiSinr = kUnknown;
        int32_t nr_csiCqiTableIndex = kUnknown;
        int32_t nr_timingAdvance = kUnknown;
    };

    struct CLCC {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CLCC"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::vector<voice::Call> calls;
    };

    struct CCFCU {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CCFCU"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::vector<voice::CallForwardInfo> callForwardInfos;
    };

    struct CCWA {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CCWA"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        int serviceClass = -1;
        bool enable = false;
    };

    struct CGDCONT {
        struct PdpContext {
            std::string type;
            std::string apn;
            std::string addr;
            int index = -1;
            int dComp = 0;
            int hComp = 0;
        };

        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CGDCONT"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::vector<PdpContext> contexts;
    };

    struct CGCONTRDP {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CGCONTRDP"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::string apn;
        std::string localAddr;
        std::string gwAddr;
        std::string dns1;
        std::string dns2;
        int cid = -1;
        int bearer = -1;
        int localAddrSize = 0;
    };

    struct CGFPCCFG {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CGFPCCFG"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        network::CellConnectionStatus status;
        ratUtils::ModemTechnology mtech;
        int contextId = -1;
        int bandwidth = -1;
        int freq = -1;
    };

    struct CUSATD {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CUSATD"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        int a = 0;
        int b = 0;
    };

    struct CUSATP {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CUSATP"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::string cmd;
    };

    struct CUSATE {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CUSATE"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::string response;
    };

    struct CUSATT {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CUSATT"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        int value = 0;
    };

    struct CUSATEND {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CUSATEND"sv;
        }
        static AtResponsePtr parse(std::string_view str);
    };

    struct CLCK {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CLCK"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        bool locked = false;
    };

    struct CSIM {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CSIM"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::string response;
    };

    struct CGLA {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CGLA"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::string response;
    };

    struct CCHC {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CCHC"sv;
        }
        static AtResponsePtr parse(std::string_view str);
    };

    struct CLIP {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CLIP"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        bool enable = false;
        voice::ClipStatus status = voice::ClipStatus::UNKNOWN;
    };

    struct CLIR {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CLIR"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        int n = -1;
        int m = -1;
    };

    struct CMUT {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CMUT"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        bool on = false;
    };

    struct WSOS {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "WSOS"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        bool isEmergencyMode = false;
    };

    struct CSCA {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CSCA"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::string sca;
        int tosca = -1;
    };

    struct CSCB {
        struct Association {
            int from;
            int to;
        };

        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CSCB"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        static std::optional<std::vector<Association>> parseIds(std::string_view str);

        std::vector<Association> serviceId;
        std::vector<Association> codeScheme;
        int mode = -1;
    };

    struct CMGS {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CMGS"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        int messageRef = -1;
    };

    struct CMGW {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CMGW"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        int messageRef = -1;
    };

    struct CMT {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CMT"sv;
        }
        static std::pair<int, AtResponsePtr> parse(std::string_view str);

        std::vector<uint8_t> pdu;
        int something = -1;
    };

    struct CDS {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CDS"sv;
        }
        static std::pair<int, AtResponsePtr> parse(std::string_view str);

        std::vector<uint8_t> pdu;
        int pduSize = -1;
    };

    struct MBAU {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "MBAU"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::vector<uint8_t> kc;
        std::vector<uint8_t> sres;
        std::vector<uint8_t> ck;
        std::vector<uint8_t> ik;
        std::vector<uint8_t> resAuts;
        int status;
    };

    struct CTZV {
        static constexpr std::string_view id() {
            using namespace std::literals;
            return "CTZV"sv;
        }
        static AtResponsePtr parse(std::string_view str);

        std::string nitzString() const;

        std::string tzName;
        uint16_t year = 0;
        uint8_t month = 0;
        uint8_t day = 0;
        uint8_t hour : 7 = 0;
        uint8_t isDaylightSaving : 1 = 0;
        uint8_t minute = 0;
        uint8_t second = 0;
        int8_t tzOffset15m = 0;
    };

    static ParseResult parse(std::string_view str);

    template <class T> static AtResponsePtr make(T v) {
        const auto r = std::make_shared<AtResponse>(std::move(v));
        const auto w = r->what();
        return r;
    }

    template <class T> static AtResponsePtr makeParseErrorFor() {
        ParseError parseError = {
            .cmd = T::id(),
        };

        return make(std::move(parseError));
    }

    bool isOK() const {
        return std::holds_alternative<OK>(value);
    }

    bool isERROR() const {
        return std::holds_alternative<ERROR>(value);
    }

    bool isParseError() const {
        return std::holds_alternative<ParseError>(value);
    }

    template <class T> bool holds() const {
        if (std::holds_alternative<T>(value)) {
            return true;
        } else if (const ParseError* e = std::get_if<ParseError>(&value)) {
            return e->cmd.compare(T::id()) == 0;
        } else {
            return false;
        }
    }

    template <> bool holds<OK>() const {
        return std::holds_alternative<OK>(value);
    }

    template <> bool holds<SmsPrompt>() const {
        return std::holds_alternative<SmsPrompt>(value);
    }

    template <> bool holds<std::string>() const {
        return std::holds_alternative<std::string>(value);
    }

    template <class T> const T* get_if() const {
        return std::get_if<T>(&value);
    }

    std::string_view what() const;

    template <class F> void visit(const F& f) const {
        std::visit(f, value);
    }

    template <class R, class F> R visitR(const F& f) const {
        return std::visit(f, value);
    }

    [[noreturn]] void unexpected(
            const char* klass, const char* request,
            std::source_location location = std::source_location::current()) const;

    template <class T> AtResponse(T v) : value(std::move(v)) {}

private:
    using Value = std::variant<OK, ParseError,
                               ERROR, RING, SmsPrompt, CmeError, CmsError,
                               CPIN, CPINR, CRSM, CFUN,
                               CREG, CEREG, CGREG,
                               CTEC, COPS, WRMP, CCSS, CSQ,
                               CLCC, CCFCU, CCWA,
                               CGDCONT, CGCONTRDP, CGFPCCFG,
                               CUSATD, CUSATP, CUSATE, CUSATT, CUSATEND,
                               CLCK, CSIM, CGLA, CCHC,
                               CLIP, CLIR, CMUT, WSOS,
                               CSCA, CSCB, CMGS, CMGW, CMT, CDS,
                               MBAU,
                               CTZV,
                               std::string
                  >;
    Value value;
};

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
