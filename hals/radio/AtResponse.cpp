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

#define FAILURE_DEBUG_PREFIX "AtResponse"

#include <algorithm>
#include <charconv>
#include <numeric>
#include <string>
#include <string_view>

#include "atCmds.h"
#include "AtResponse.h"
#include "Parser.h"
#include "debug.h"
#include "hexbin.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {
using namespace std::literals;

namespace {
constexpr char kCR = '\r';
constexpr std::string_view krOKr = "\rOK\r"sv;

struct ValueParser {
    std::string_view cmd;
    AtResponsePtr (*parse)(std::string_view str);
    bool multiline;
};

struct CmdIdVisitor {
    std::string_view operator()(const AtResponse::OK&) const {
        return "OK"sv;
    }

    std::string_view operator()(const AtResponse::ERROR&) const {
        return "ERROR"sv;
    }

    std::string_view operator()(const AtResponse::RING&) const {
        return "RING"sv;
    }

    std::string_view operator()(const AtResponse::SmsPrompt&) const {
        return "SmsPrompt"sv;
    }

    std::string_view operator()(const AtResponse::ParseError&) const {
        return "ParseError"sv;
    }

    std::string_view operator()(const std::string&) const {
        return "string"sv;
    }

    template <class T> std::string_view operator()(const T&) const {
        return T::id();
    }
};

std::string_view ltrim(std::string_view s) {
    while (!s.empty()) {
        if (s.front() <= 0x20) {
            s.remove_prefix(1);
        } else {
            break;
        }
    }
    return s;
}

std::string toString(std::string_view s) {
    return std::string(s.data(), s.size());
}

AtResponse::ParseResult parseCmds(const std::string_view str,
                                  const ValueParser* vp,
                                  const ValueParser* const vpEnd) {
    const std::string_view str1 = str.substr(1);  // skip + or %
    bool maybeIncomplete = false;

    for (; vp != vpEnd; ++vp) {
        const std::string_view& cmd = vp->cmd;

        if (str1.starts_with(cmd)) {
            size_t skipSize;
            std::string_view payload;

            if (str1.size() <= cmd.size()) {
                maybeIncomplete = true;
                continue;
            } else if (str1[cmd.size()] == ':') {
                skipSize = 1 + cmd.size() + 1; // `+CMD:`
            } else if (str1[cmd.size()] == '\r') {
                skipSize = 1 + cmd.size(); // `+CMD`
            } else {
                continue;
            }

            int consumed;

            if (vp->multiline) {
                const size_t payloadEnd = str.find(krOKr, skipSize);
                if (payloadEnd != str.npos) {
                    // keep '+CMD:' and add extra '\r' to keep lines consistent
                    payload = str.substr(0, payloadEnd + 1);
                    consumed = payloadEnd + krOKr.size();
                } else {
                    return { 0, nullptr };
                }
            } else {
                const size_t payloadEnd = str.find(kCR, skipSize);
                if (payloadEnd != str.npos) {
                    payload = ltrim(str.substr(skipSize, payloadEnd - skipSize));
                    consumed = payloadEnd + 1;
                } else {
                    return { 0, nullptr };
                }
            }

            return { consumed, (*vp->parse)(payload) };
        }
    }

    if (maybeIncomplete) {
        return { 0, nullptr };
    } else {
        return { -1, FAILURE(nullptr) };
    }
}
}  // namespace

AtResponse::ParseResult AtResponse::parse(const std::string_view str) {
#define CMD(C) AtResponse::C::id(), &AtResponse::C::parse
    static const ValueParser plusValueParsers[] = {
        { CMD(CPIN),        false },
        { CMD(CPINR),       false },
        { CMD(CRSM),        false },
        { CMD(CFUN),        false },
        { CMD(CREG),        false },
        { CMD(CEREG),       false },
        { CMD(CGREG),       false },
        { CMD(CTEC),        false },
        { CMD(COPS),        true },
        { CMD(WRMP),        false },
        { CMD(CCSS),        false },
        { CMD(CSQ),         false },
        { CMD(CLCC),        true },
        { CMD(CCFCU),       true },
        { CMD(CCWA),        false },
        { CMD(CUSATD),      false },
        { CMD(CUSATP),      false },
        { CMD(CUSATE),      false },
        { CMD(CUSATT),      false },
        { CMD(CUSATEND),    false },
        { CMD(CGDCONT),     true },
        { CMD(CGCONTRDP),   false },
        { CMD(CLCK),        false },
        { CMD(CSIM),        false },
        { CMD(CCHC),        false },
        { CMD(CSCA),        false },
        { CMD(CSCB),        false },
        { CMD(CMGS),        false },
        { CMD(CMGW),        false },
        { CMD(CmeError),    false },
        { CMD(CmsError),    false },
    };

    static const ValueParser percentValueParsers[] = {
        { CMD(CTZV),     false },
        { CMD(CGFPCCFG), false },
    };

    static const ValueParser caretValueParsers[] = {
        { CMD(MBAU),     false },
    };
#undef CMD

    static constexpr std::string_view kRING = "RING\r"sv;
    if (str.starts_with(kRING)) {
        return { int(kRING.size()), AtResponse::make(RING()) };
    }

    static constexpr std::string_view kCMT = "+CMT:"sv;
    if (str.starts_with(kCMT)) {
        const std::string_view trimmed = ltrim(str.substr(kCMT.size()));
        const auto [consumed, response] = AtResponse::CMT::parse(trimmed);
        if (consumed > 0) {
            return { int(consumed + str.size() - trimmed.size()), std::move(response) };
        } else {
            return { consumed, nullptr };
        }
    }

    static constexpr std::string_view kCDS = "+CDS:"sv;
    if (str.starts_with(kCDS)) {
        const std::string_view trimmed = ltrim(str.substr(kCDS.size()));
        const auto [consumed, response] = AtResponse::CDS::parse(trimmed);
        if (consumed > 0) {
            return { int(consumed + str.size() - trimmed.size()), std::move(response) };
        } else {
            return { consumed, nullptr };
        }
    }

    switch (str.front()) {
    case '+': return parseCmds(str,
                               std::begin(plusValueParsers),
                               std::end(plusValueParsers));

    case '%': return parseCmds(str,
                               std::begin(percentValueParsers),
                               std::end(percentValueParsers));
    case '^': return parseCmds(str,
                               std::begin(caretValueParsers),
                               std::end(caretValueParsers));
    }

    static constexpr std::string_view kSmsPrompt = "> \r"sv;
    if (str.starts_with(kSmsPrompt)) {
        return { int(kSmsPrompt.size()), AtResponse::make(SmsPrompt()) };
    }

    static constexpr std::string_view kOKr = "OK\r"sv;
    if (str.starts_with(kOKr)) {
        return { int(kOKr.size()), AtResponse::make(OK()) };
    }

    static constexpr std::string_view kERRORr = "ERROR\r"sv;
    if (str.starts_with(kERRORr)) {
        return { int(kERRORr.size()), AtResponse::make(ERROR()) };
    }

    const size_t pos = str.find(krOKr);
    if (pos != str.npos) {
        std::string value(str.begin(), str.begin() + pos);
        return { int(pos + krOKr.size()), make(std::move(value)) };
    }

    return { 0, nullptr };
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CmeError"
AtResponsePtr AtResponse::CmeError::parse(const std::string_view str) {
    RadioError err;

    if (str.compare(atCmds::kCmeErrorOperationNotAllowed) == 0) {
        err = RadioError::OPERATION_NOT_ALLOWED;
    } else if (str.compare(atCmds::kCmeErrorOperationNotSupported) == 0) {
        err = RadioError::REQUEST_NOT_SUPPORTED;
    } else if (str.compare(atCmds::kCmeErrorSimNotInserted) == 0) {
        err = RadioError::SIM_ABSENT;
    } else if (str.compare(atCmds::kCmeErrorSimPinRequired) == 0) {
        err = RadioError::SIM_PIN2;
    } else if (str.compare(atCmds::kCmeErrorSimPukRequired) == 0) {
        err = RadioError::SIM_PUK2;
    } else if (str.compare(atCmds::kCmeErrorSimBusy) == 0) {
        err = RadioError::SIM_BUSY;
    } else if (str.compare(atCmds::kCmeErrorIncorrectPassword) == 0) {
        err = RadioError::PASSWORD_INCORRECT;
    } else if (str.compare(atCmds::kCmeErrorMemoryFull) == 0) {
        err = RadioError::SIM_FULL;
    } else if (str.compare(atCmds::kCmeErrorInvalidIndex) == 0) {
        err = RadioError::INVALID_ARGUMENTS;
    } else if (str.compare(atCmds::kCmeErrorNotFound) == 0) {
        err = RadioError::NO_SUCH_ELEMENT;
    } else if (str.compare(atCmds::kCmeErrorInvalidCharactersInTextString) == 0) {
        err = RadioError::GENERIC_FAILURE;
    } else if (str.compare(atCmds::kCmeErrorNoNetworkService) == 0) {
        err = RadioError::NO_NETWORK_FOUND;
    } else if (str.compare(atCmds::kCmeErrorNetworkNotAllowedEmergencyCallsOnly) == 0) {
        err = RadioError::NETWORK_REJECT;
    } else if (str.compare(atCmds::kCmeErrorInCorrectParameters) == 0) {
        err = RadioError::INVALID_ARGUMENTS;
    } else if (str.compare(atCmds::kCmeErrorNetworkNotAttachedDueToMTFunctionalRestrictions) == 0) {
        err = RadioError::NETWORK_REJECT;
    } else if (str.compare(atCmds::kCmeErrorFixedDialNumberOnlyAllowed) == 0) {
        err = RadioError::GENERIC_FAILURE;
    } else {
        err = RadioError::GENERIC_FAILURE;
    }

    CmeError cmeErr = {
        .error = err,
    };

    return make(std::move(cmeErr));
}

RadioError AtResponse::CmeError::getErrorAndLog(
        const char* klass, const char* func, int line) const {
    RLOGE("%s:%s:%d failure: %s", klass, func, line,
          toString(error).c_str());
    return error;
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CmsError"
AtResponsePtr AtResponse::CmsError::parse(const std::string_view str) {
    CmsError cmsErr = {
        .message = toString(str),
    };

    return make(std::move(cmsErr));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CPIN"
AtResponsePtr AtResponse::CPIN::parse(const std::string_view str) {
    CPIN cpin;
    if (str == "READY"sv) {
        cpin.state = CPIN::State::READY;
    } else if (str == "SIM PIN"sv) {
        cpin.state = CPIN::State::PIN;
    } else if (str == "SIM PUK"sv) {
        cpin.state = CPIN::State::PUK;
    } else {
        return FAILURE_V(makeParseErrorFor<CPIN>(),
                         "Can't parse: '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    return make(std::move(cpin));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CPINR"
AtResponsePtr AtResponse::CPINR::parse(const std::string_view str) {
    CPINR cpinr;

    Parser parser(str);
    std::string_view unused;
    if (!parser(&unused, ',')
               (&cpinr.remainingRetryTimes).skip(',')
               (&cpinr.maxRetryTimes).skip(',').fullMatch()) {
        return FAILURE_V(makeParseErrorFor<CPINR>(),
                         "Can't parse: '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    return make(std::move(cpinr));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CRSM"
AtResponsePtr AtResponse::CRSM::parse(const std::string_view str) {
    CRSM crsm;

    Parser parser(str);
    if (parser(&crsm.sw1).skip(',')(&crsm.sw2).hasMore()) {
        if (parser.skip(',').matchSoFar()) {
            crsm.response = toString(parser.remaining());
        } else {
            return FAILURE_V(makeParseErrorFor<CRSM>(),
                             "Can't parse: '%*.*s'",
                             int(str.size()), int(str.size()), str.data());
        }
    } else if (!parser.fullMatch()) {
        return FAILURE_V(makeParseErrorFor<CRSM>(),
                         "Can't parse: '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    return make(std::move(crsm));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CFUN"
AtResponsePtr AtResponse::CFUN::parse(const std::string_view str) {
    using modem::RadioState;

    int state;
    Parser parser(str);
    if (parser(&state).fullMatch()) {
        CFUN cfun = {
            .state = state ? RadioState::ON : RadioState::OFF,
        };
        return make(std::move(cfun));
    } else {
        return FAILURE_V(makeParseErrorFor<CFUN>(),
                         "Can't parse: '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CREG"
AtResponsePtr AtResponse::CREG::parse(const std::string_view str) {
    CREG creg;
    Parser parser(str);
    std::string_view areaCodeHex;
    std::string_view cellIdHex;
    int unsolMode;
    int state;

    switch (std::count(str.begin(), str.end(), ',')) {
    case 0:  // state
        if (parser(&state).fullMatch()) {
            creg.state = static_cast<network::RegState>(state);
            return make(std::move(creg));
        }
        break;

    case 1:  // unsolMode,state
        if (parser(&unsolMode).skip(',')(&state).fullMatch()) {
            creg.state = static_cast<network::RegState>(state);
            return make(std::move(creg));
        }
        break;

    case 3:  // state,areaCode,cellId,networkType
        if (parser(&state).skip(',').skip('"')(&areaCodeHex, '"')
                  .skip(',').skip('"')(&cellIdHex, '"').skip(',')
                  (&creg.networkType).fullMatch()) {
            std::from_chars(areaCodeHex.begin(), areaCodeHex.end(), creg.areaCode, 16);
            std::from_chars(cellIdHex.begin(), cellIdHex.end(), creg.cellId, 16);
            creg.state = static_cast<network::RegState>(state);
            return make(std::move(creg));
        }
        break;

    case 4:  // unsolMode,state,areaCode,cellId,networkType
        if (parser(&unsolMode).skip(',')
                  (&state).skip(',')
                  .skip('"')(&areaCodeHex, '"').skip(',')
                  .skip('"')(&cellIdHex, '"').skip(',')
                  (&creg.networkType).fullMatch()) {
            std::from_chars(areaCodeHex.begin(), areaCodeHex.end(), creg.areaCode, 16);
            std::from_chars(cellIdHex.begin(), cellIdHex.end(), creg.cellId, 16);
            creg.state = static_cast<network::RegState>(state);
            return make(std::move(creg));
        }
        break;
    }

    return FAILURE_V(makeParseErrorFor<CREG>(),
                     "Can't parse: '%*.*s'",
                     int(str.size()), int(str.size()), str.data());
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CGREG"
AtResponsePtr AtResponse::CGREG::parse(const std::string_view str) {
    CGREG cgreg;
    Parser parser(str);
    std::string_view areaCodeHex;
    std::string_view cellIdHex;
    int unsolMode;
    int state;

    switch (std::count(str.begin(), str.end(), ',')) {
    case 0:  // state
        if (parser(&state).fullMatch()) {
            cgreg.state = static_cast<network::RegState>(state);
            return make(std::move(cgreg));
        }
        break;

    case 1:  // unsolMode,state
        if (parser(&unsolMode).skip(',')(&state).fullMatch()) {
            cgreg.state = static_cast<network::RegState>(state);
            return make(std::move(cgreg));
        }
        break;

    case 3:  // state,areaCode,cellId,networkType
        if (parser(&state).skip(',').skip('"')(&areaCodeHex, '"')
                  .skip(',').skip('"')(&cellIdHex, '"').skip(',')
                  (&cgreg.networkType).fullMatch()) {
            std::from_chars(areaCodeHex.begin(), areaCodeHex.end(), cgreg.areaCode, 16);
            std::from_chars(cellIdHex.begin(), cellIdHex.end(), cgreg.cellId, 16);
            cgreg.state = static_cast<network::RegState>(state);
            return make(std::move(cgreg));
        }
        break;

    case 4:  // unsolMode,state,areaCode,cellId,networkType
        if (parser(&unsolMode).skip(',')
                  (&state).skip(',')
                  .skip('"')(&areaCodeHex, '"').skip(',')
                  .skip('"')(&cellIdHex, '"').skip(',')
                  (&cgreg.networkType).fullMatch()) {
            std::from_chars(areaCodeHex.begin(), areaCodeHex.end(), cgreg.areaCode, 16);
            std::from_chars(cellIdHex.begin(), cellIdHex.end(), cgreg.cellId, 16);
            cgreg.state = static_cast<network::RegState>(state);
            return make(std::move(cgreg));
        }
        break;
    }

    return FAILURE_V(makeParseErrorFor<CGREG>(),
                     "Can't parse: '%*.*s'",
                     int(str.size()), int(str.size()), str.data());
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CEREG"
AtResponsePtr AtResponse::CEREG::parse(const std::string_view str) {
    CEREG cereg;
    Parser parser(str);
    std::string_view areaCodeHex;
    std::string_view cellIdHex;
    int unsolMode;
    int state;

    switch (std::count(str.begin(), str.end(), ',')) {
    case 0:  // state
        if (parser(&state).fullMatch()) {
            cereg.state = static_cast<network::RegState>(state);
            return make(std::move(cereg));
        }
        break;

    case 1:  // unsolMode,state
        if (parser(&unsolMode).skip(',')(&state).fullMatch()) {
            cereg.state = static_cast<network::RegState>(state);
            return make(std::move(cereg));
        }
        break;

    case 3:  // state,areaCode,cellId,networkType
        if (parser(&state).skip(',').skip('"')(&areaCodeHex, '"')
                  .skip(',').skip('"')(&cellIdHex, '"').skip(',')
                  (&cereg.networkType).fullMatch()) {
            std::from_chars(areaCodeHex.begin(), areaCodeHex.end(), cereg.areaCode, 16);
            std::from_chars(cellIdHex.begin(), cellIdHex.end(), cereg.cellId, 16);
            cereg.state = static_cast<network::RegState>(state);
            return make(std::move(cereg));
        }
        break;

    case 4:  // unsolMode,state,areaCode,cellId,networkType
        if (parser(&unsolMode).skip(',')
                  (&state).skip(',')
                  .skip('"')(&areaCodeHex, '"').skip(',')
                  .skip('"')(&cellIdHex, '"').skip(',')
                  (&cereg.networkType).fullMatch()) {
            std::from_chars(areaCodeHex.begin(), areaCodeHex.end(), cereg.areaCode, 16);
            std::from_chars(cellIdHex.begin(), cellIdHex.end(), cereg.cellId, 16);
            cereg.state = static_cast<network::RegState>(state);
            return make(std::move(cereg));
        }
        break;
    }

    return FAILURE_V(makeParseErrorFor<CEREG>(),
                     "Can't parse: '%*.*s'",
                     int(str.size()), int(str.size()), str.data());
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CTEC"
/*      +CTEC: current (decimal),preferred_bitmask (hex)
 *  OR
 *      +CTEC: comma_separated_list_of_supported (decimal)
 *  OR
 *      +CTEC: current (decimal)
 *  OR
 *      +CTEC: DONE
*/
AtResponsePtr AtResponse::CTEC::parse(const std::string_view str) {
    CTEC ctec;

    size_t i = 0;
    while (true) {
        const size_t comma = str.find(',', i);
        if (comma != std::string_view::npos) {
            ctec.values.push_back(std::string(str.substr(i, comma - i)));
            i = comma + 1;
        } else {
            ctec.values.push_back(std::string(str.substr(i)));
            break;
        }
    }

    return make(std::move(ctec));
}

std::optional<ratUtils::ModemTechnology> AtResponse::CTEC::getCurrentModemTechnology() const {
    using ratUtils::ModemTechnology;

    if ((values.size() == 0) || (values.size() > 2) ||
            ((values.size() == 1) && (values[0] == "DONE"))) {
        return FAILURE(std::nullopt);
    }

    int mtech;
    std::from_chars_result r =
        std::from_chars(&*values[0].begin(), &*values[0].end(), mtech, 10);

    if ((r.ec != std::errc()) || (r.ptr != &*values[0].end())) {
        return FAILURE(std::nullopt);
    }

    for (unsigned i = static_cast<unsigned>(ModemTechnology::GSM);
                  i <= static_cast<unsigned>(ModemTechnology::NR); ++i) {
        if (mtech & (1U << i)) {
            return static_cast<ratUtils::ModemTechnology>(i);
        }
    }

    return FAILURE(std::nullopt);
}

bool AtResponse::CTEC::isDONE() const {
    return (values.size() == 1) && (values[0] == "DONE");
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "COPS"
/*      "+COPS: 0,0,longName\r+COPS: 0,1,shortName\r+COPS: 0,2,numeric\r"
 *  OR
 *      "+COPS: (state,longName,shortName,numeric),(...),...\r"
 *  OR
 *      "+COPS: selectionMode,2,numeric\r"
 *  OR
 *      "+COPS: selectionMode,0,0\r"
 *  OR
 *      "+COPS: 0,0,0\r"
 */
AtResponsePtr AtResponse::COPS::parse(const std::string_view str) {
    COPS cops;

    Parser parser(str);
    if (!parser.skip("+COPS:").skip(' ').hasMore()) { goto err; }

    if (parser.front() == '(') {
        while (parser.matchSoFar()) {
            COPS::OperatorInfo operatorInfo;
            int state;

            if (parser.skip('(')(&state).skip(',')
                                (&operatorInfo.longName, ',')
                                (&operatorInfo.shortName, ',')
                                (&operatorInfo.numeric, ')').matchSoFar()) {
                operatorInfo.state = static_cast<COPS::OperatorInfo::State>(state);
                cops.operators.push_back(std::move(operatorInfo));

                if (parser.front() == ',') {
                    parser.skip(',');
                } else {
                    break;
                }
            } else {
                goto err;
            }
        }

        return make(std::move(cops));
    } else {
        std::string str;
        int networkSelectionMode, n;

        if (!parser(&networkSelectionMode).skip(',')(&n).skip(',')(&str, kCR).matchSoFar()) {
            goto err;
        }

        if ((n == 2) && parser.fullMatch()) {
            cops.networkSelectionMode = static_cast<COPS::NetworkSelectionMode>(networkSelectionMode);
            cops.numeric = std::move(str);
            return make(std::move(cops));
        } else if (n != 0) {
            goto err;
        } else if ((str == "0") && parser.fullMatch()) {
            cops.networkSelectionMode = static_cast<COPS::NetworkSelectionMode>(networkSelectionMode);
            return make(std::move(cops));
        }

        COPS::OperatorInfo operatorInfo;
        operatorInfo.state = COPS::OperatorInfo::State::CURRENT;
        operatorInfo.longName = std::move(str);

        if (!parser.skip("+COPS:").skip(' ').skip("0,1,")(&operatorInfo.shortName, kCR)
                   .skip("+COPS:").skip(' ').skip("0,2,")(&operatorInfo.numeric, kCR)
                   .fullMatch()) {
            goto err;
        }

        cops.operators.push_back(std::move(operatorInfo));
        return make(std::move(cops));
    }

err:
    return FAILURE_V(makeParseErrorFor<COPS>(),
                     "Can't parse: '%*.*s'",
                     int(str.size()), int(str.size()), str.data());
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "WRMP"
AtResponsePtr AtResponse::WRMP::parse(const std::string_view str) {
    int cdmaRoamingPreference;

    Parser parser(str);
    if (!parser(&cdmaRoamingPreference).fullMatch()) {
        return FAILURE_V(makeParseErrorFor<WRMP>(),
                         "Can't parse: '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    WRMP wrmp = {
        .cdmaRoamingPreference =
            static_cast<network::CdmaRoamingType>(cdmaRoamingPreference),
    };

    return make(std::move(wrmp));
}


#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CCSS"
AtResponsePtr AtResponse::CCSS::parse(const std::string_view str) {
    CCSS ccss;
    int source;

    Parser parser(str);
    if (parser(&source).fullMatch()) {
        ccss.source = static_cast<sim::CdmaSubscriptionSource>(source);
        return make(std::move(ccss));
    } else {
        return FAILURE_V(makeParseErrorFor<CCSS>(),
                         "Can't parse: '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CSQ"
AtResponsePtr AtResponse::CSQ::parse(const std::string_view str) {
    constexpr size_t kMaxSize = 22;
    int values[kMaxSize];

    Parser parser(str);
    if (!parser(&values[0]).matchSoFar()) {
        return FAILURE_V(makeParseErrorFor<CSQ>(),
                         "Can't parse: '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    size_t n;
    for (n = 1; parser.hasMore() && (n < kMaxSize); ++n) {
        if (!parser.skip(',')(&values[n]).matchSoFar()) {
            return FAILURE_V(makeParseErrorFor<CSQ>(),
                             "Can't parse: '%*.*s'",
                             int(str.size()), int(str.size()), str.data());
        }
    }

    if (!parser.fullMatch()) {
        return FAILURE_V(makeParseErrorFor<CSQ>(),
                         "Can't parse: '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    CSQ csq;
    switch (n) {
    case 22:
        csq.wcdma_signalStrength = values[14];
        if (csq.wcdma_signalStrength != kUnknown) {
            csq.wcdma_rscp = 42;
            csq.wcdma_ecno = 19;
        }
        csq.wcdma_bitErrorRate = values[15];
        csq.nr_ssRsrp = values[16];
        csq.nr_ssRsrq = values[17];
        csq.nr_ssSinr = values[18];
        csq.nr_csiRsrp = values[19];
        csq.nr_csiRsrq = values[20];
        csq.nr_csiSinr = values[21];
        [[fallthrough]];

    case 14:
        csq.tdscdma_rscp = values[13];
        [[fallthrough]];

    case 13:
        csq.lte_timingAdvance = values[12];
        [[fallthrough]];

    case 12:
        csq.gsm_signalStrength = values[0];
        csq.gsm_bitErrorRate = values[1];
        csq.cdma_dbm = values[2];
        csq.cdma_ecio = values[3];
        csq.evdo_dbm = values[4];
        csq.evdo_ecio = values[5];
        csq.evdo_signalNoiseRatio = values[6];
        csq.lte_signalStrength = values[7];
        csq.lte_rsrp = values[8];
        csq.lte_rsrq = values[9];
        csq.lte_rssnr = values[10];
        csq.lte_cqi = values[11];
        break;

    default:
        return FAILURE_V(makeParseErrorFor<CSQ>(),
                         "Unexpected size: %zu", n);
    }

    return make(std::move(csq));
}

network::SignalStrength AtResponse::CSQ::toSignalStrength() const {
    return {
        .gsm = {
            .signalStrength = gsm_signalStrength,
            .bitErrorRate = gsm_bitErrorRate,
            .timingAdvance = gsm_timingAdvance,
        },
        .cdma = {
            .dbm = cdma_dbm,
            .ecio = cdma_ecio,
        },
        .evdo = {
            .dbm = evdo_dbm,
            .ecio = evdo_ecio,
            .signalNoiseRatio = evdo_signalNoiseRatio,
        },
        .lte = {
            .signalStrength = lte_signalStrength,
            .rsrp = lte_rsrp,
            .rsrq = lte_rsrq,
            .rssnr = lte_rssnr,
            .cqi = lte_cqi,
            .timingAdvance = lte_timingAdvance,
            .cqiTableIndex = lte_cqiTableIndex,
        },
        .tdscdma = {
            .signalStrength = tdscdma_signalStrength,
            .bitErrorRate = tdscdma_bitErrorRate,
            .rscp = tdscdma_rscp,
        },
        .wcdma = {
            .signalStrength = wcdma_signalStrength,
            .bitErrorRate = wcdma_bitErrorRate,
            .rscp = wcdma_rscp,
            .ecno = wcdma_ecno,
        },
        .nr = {
            .ssRsrp = nr_ssRsrp,
            .ssRsrq = nr_ssRsrq,
            .ssSinr = nr_ssSinr,
            .csiRsrp = nr_csiRsrp,
            .csiRsrq = nr_csiRsrq,
            .csiSinr = nr_csiSinr,
            .csiCqiTableIndex = nr_csiCqiTableIndex,
            .timingAdvance = nr_timingAdvance,
        },
    };
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CLCC"
AtResponsePtr AtResponse::CLCC::parse(const std::string_view str) {
    CLCC clcc;

    Parser parser(str);
    while (parser.hasMore()) {
        int index;
        int dir;
        int state;
        int mode;
        int mpty;
        int type;
        std::string number;

        // +CLCC: <index>,<dir>,<state>,<mode>,<mpty>,<number>,<type>\r
        if (parser.skip("+CLCC:").skip(' ')(&index).skip(',')
                  (&dir).skip(',')(&state).skip(',')
                  (&mode).skip(',')(&mpty).skip(',')
                  (&number, ',')(&type).skip(kCR).matchSoFar()) {

            voice::Call call = {
                .state = state,
                .index = index,
                .toa = type,
                .isMpty = (mpty != 0),
                .isMT = (dir != 0),
                .isVoice = (mode == 0),
                .number = std::move(number),
            };

            clcc.calls.push_back(std::move(call));
        } else {
            return FAILURE_V(makeParseErrorFor<CLCC>(),
                             "Can't parse '%*.*s'",
                             int(str.size()), int(str.size()), str.data());
        }
    }

    return make(std::move(clcc));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CCFCU"
AtResponsePtr AtResponse::CCFCU::parse(const std::string_view str) {
    CCFCU ccfcu;

    Parser parser(str);
    while (parser.hasMore()) {
        voice::CallForwardInfo cfi;
        int numberType;
        std::string_view ignore;
        if (parser.skip("+CCFCU:").skip(' ')(&cfi.status).skip(',')
                (&cfi.serviceClass).skip(',')(&numberType).skip(',')
                (&cfi.toa).skip(',').skip('"')(&cfi.number, '"').matchSoFar()) {
            switch (parser.front()) {
            case ',':
                if (!parser.skip(',')(&ignore, ',')(&ignore, ',')
                          (&ignore, ',')(&cfi.timeSeconds).skip(kCR).matchSoFar()) {
                    return FAILURE_V(makeParseErrorFor<CCFCU>(),
                                     "Can't parse '%*.*s'",
                                     int(str.size()), int(str.size()), str.data());
                }
                break;

            case kCR:
                parser.skip(kCR);
                break;

            default:
                return FAILURE_V(makeParseErrorFor<CCFCU>(),
                                 "Can't parse '%*.*s'",
                                 int(str.size()), int(str.size()), str.data());
            }

            ccfcu.callForwardInfos.push_back(std::move(cfi));
        } else {
            return FAILURE_V(makeParseErrorFor<CCFCU>(),
                             "Can't parse '%*.*s'",
                             int(str.size()), int(str.size()), str.data());
        }
    }

    return make(std::move(ccfcu));
}


#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CCWA"
AtResponsePtr AtResponse::CCWA::parse(const std::string_view str) {
    CCWA ccwa;
    int mode;

    Parser parser(str);
    if (parser(&mode).skip(',')(&ccwa.serviceClass).fullMatch()) {
        ccwa.enable = (mode == 1);
        return make(std::move(ccwa));
    } else {
        return FAILURE_V(makeParseErrorFor<CCWA>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CUSATD"
AtResponsePtr AtResponse::CUSATD::parse(const std::string_view str) {
    CUSATD cusatd;

    Parser parser(str);
    if (parser(&cusatd.a).skip(',').skip(' ')(&cusatd.a).fullMatch()) {
        return make(std::move(cusatd));
    } else {
        return FAILURE_V(makeParseErrorFor<CUSATD>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CUSATP"
AtResponsePtr AtResponse::CUSATP::parse(const std::string_view str) {
    CUSATP cusatp = {
        .cmd = toString(str),
    };

    return make(std::move(cusatp));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CUSATE"
AtResponsePtr AtResponse::CUSATE::parse(const std::string_view str) {
    CUSATE cusate = {
        .response = toString(str),
    };

    return make(std::move(cusate));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CUSATT"
AtResponsePtr AtResponse::CUSATT::parse(const std::string_view str) {
    CUSATT cusatt;

    Parser parser(str);
    if (parser(&cusatt.value).fullMatch()) {
        return make(std::move(cusatt));
    } else {
        return FAILURE_V(makeParseErrorFor<CUSATT>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CUSATEND"
AtResponsePtr AtResponse::CUSATEND::parse(const std::string_view) {
    return make(CUSATEND());
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CLCK"
AtResponsePtr AtResponse::CLCK::parse(const std::string_view str) {
    CLCK clck;

    switch (str.front()) {
    case '0': clck.locked = false; break;
    case '1': clck.locked = true; break;
    default:
        return FAILURE_V(makeParseErrorFor<CLCK>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    return make(std::move(clck));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CSIM"
AtResponsePtr AtResponse::CSIM::parse(const std::string_view str) {
    Parser parser(str);
    int len;

    if (parser(&len).skip(',').matchSoFar()) {
        const std::string_view response = parser.remaining();

        if (len == response.size()) {
            CSIM csim = {
                .response = toString(response),
            };

            return make(std::move(csim));
        }
    }

    return FAILURE_V(makeParseErrorFor<CSIM>(),
                     "Can't parse '%*.*s'",
                     int(str.size()), int(str.size()), str.data());
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CGLA"
AtResponsePtr AtResponse::CGLA::parse(const std::string_view str) {
    Parser parser(str);
    int len;

    if (parser(&len).skip(',').matchSoFar()) {
        const std::string_view response = parser.remaining();

        if (len == response.size()) {
            CGLA cgla = {
                .response = toString(response),
            };

            return make(std::move(cgla));
        }
    }

    return FAILURE_V(makeParseErrorFor<CGLA>(),
                     "Can't parse '%*.*s'",
                     int(str.size()), int(str.size()), str.data());
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CCHC"
AtResponsePtr AtResponse::CCHC::parse(const std::string_view) {
    return make(CCHC());
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CSCA"
AtResponsePtr AtResponse::CSCA::parse(const std::string_view str) {
    CSCA csca;

    Parser parser(str);
    if (!parser(&csca.sca, ',')(&csca.tosca).fullMatch()) {
        return FAILURE_V(makeParseErrorFor<CSCA>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    return make(std::move(csca));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CSCB"
AtResponsePtr AtResponse::CSCB::parse(const std::string_view str) {
    CSCB cscb;
    std::string_view serviceId;
    std::string_view codeScheme;

    Parser parser(str);
    if (!parser(&cscb.mode).skip(',')
               .skip('"')(&serviceId, '"').skip(',')
               .skip('"')(&codeScheme, '"').fullMatch()) {
fail:   return FAILURE_V(makeParseErrorFor<CSCB>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    auto maybeIds = parseIds(serviceId);
    if (!maybeIds) {
        goto fail;
    }
    cscb.serviceId = std::move(maybeIds.value());

    maybeIds = parseIds(codeScheme);
    if (!maybeIds) {
        goto fail;
    }
    cscb.codeScheme = std::move(maybeIds.value());

    return make(std::move(cscb));
}

std::optional<std::vector<AtResponse::CSCB::Association>>
AtResponse::CSCB::parseIds(const std::string_view str) {
    std::vector<Association> ids;
    Parser parser(str);
    while (parser.hasMore()) {
        Association a;
        if (!parser(&a.from).matchSoFar()) {
            return std::nullopt;
        }

        if (parser.fullMatch()) {
            a.to = a.from;
            ids.push_back(a);
            break;
        }

        switch (parser.front()) {
        case '-':
            if (!parser.skip('-')(&a.to).matchSoFar()) {
                return std::nullopt;
            }
            ids.push_back(a);
            if (parser.fullMatch()) {
                break;
            } else if (parser.front() == ',') {
                parser.skip(',');
            } else {
                return std::nullopt;
            }
            break;

        case ',':
            parser.skip(',');
            break;

        default:
            return std::nullopt;
        }
    }

    return ids;
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CMGS"
AtResponsePtr AtResponse::CMGS::parse(const std::string_view str) {
    CMGS cmgs;

    Parser parser(str);
    if (!parser(&cmgs.messageRef).fullMatch()) {
        return FAILURE_V(makeParseErrorFor<CMGS>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    return make(std::move(cmgs));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CMGW"
AtResponsePtr AtResponse::CMGW::parse(const std::string_view str) {
    CMGW cmgw;

    Parser parser(str);
    if (!parser(&cmgw.messageRef).fullMatch()) {
        return FAILURE_V(makeParseErrorFor<CMGS>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    return make(std::move(cmgw));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CMT"
std::pair<int, AtResponsePtr> AtResponse::CMT::parse(const std::string_view str) {
    CMT cmt;
    std::string strPdu;

    Parser parser(str);
    if (parser(&cmt.something).skip(kCR).matchSoFar()) {
        if (parser(&strPdu, kCR).matchSoFar()) {
            if (hex2bin(strPdu, &cmt.pdu)) {
                return { parser.consumed(), make(std::move(cmt)) };
            }
        } else {
            return { 0, nullptr };
        }
    }

    auto err = std::make_pair(-1, makeParseErrorFor<CMT>());
    return FAILURE_V(err, "Can't parse '%*.*s'",
                     int(str.size()), int(str.size()), str.data());
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CDS"
std::pair<int, AtResponsePtr> AtResponse::CDS::parse(const std::string_view str) {
    CDS cds;
    std::string strPdu;

    Parser parser(str);
    if (parser(&cds.pduSize).skip(kCR).matchSoFar()) {
        if (parser(&strPdu, kCR).matchSoFar()) {
            if (hex2bin(strPdu, &cds.pdu)) {
                return { parser.consumed(), make(std::move(cds)) };
            }
        } else {
            return { 0, nullptr };
        }
    }

    auto err = std::make_pair(-1, makeParseErrorFor<CDS>());
    return FAILURE_V(err, "Can't parse '%*.*s'",
                     int(str.size()), int(str.size()), str.data());
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CGDCONT"
// +CGDCONT: <cid>,<pdp_type>,<APN>,<pdp_addr>,<d_comp>,<h_comp>\r
// 1,"IPV6","fast.t-mobile.com",,0,0
AtResponsePtr AtResponse::CGDCONT::parse(const std::string_view str) {
    CGDCONT cgdcont;

    Parser parser(str);
    while (parser.hasMore()) {
        CGDCONT::PdpContext pdpContext;

        if (parser.skip("+CGDCONT:").skip(' ')(&pdpContext.index).skip(',')
                  .skip('"')(&pdpContext.type, '"').skip(',')
                  .skip('"')(&pdpContext.apn, '"').skip(',')
                  (&pdpContext.addr, ',')(&pdpContext.dComp)
                  .skip(',')(&pdpContext.hComp).skip(' ').matchSoFar()) {
            cgdcont.contexts.push_back(std::move(pdpContext));
        } else {
            return FAILURE_V(makeParseErrorFor<CGDCONT>(),
                             "Can't parse '%*.*s'",
                             int(str.size()), int(str.size()), str.data());
        }
    }

    return make(std::move(cgdcont));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CGCONTRDP"
// 1,5,"epc.tmobile.com",10.0.2.15/24,10.0.2.2,10.0.2.3
// 1,5,"epc.tmobile.com",10.0.2.15,10.0.2.2,10.0.2.3
AtResponsePtr AtResponse::CGCONTRDP::parse(const std::string_view str) {
    CGCONTRDP cgcontrdp;
    std::string_view unused;
    std::string_view localAddr;

    Parser parser(str);
    if (parser(&cgcontrdp.cid).skip(',')(&cgcontrdp.bearer).skip(',')
               .skip('"')(&cgcontrdp.apn, '"').skip(',')
               (&localAddr, ',')
               (&cgcontrdp.gwAddr, ',').matchSoFar()) {
        cgcontrdp.dns1 = parser.remainingAsString();
    } else {
        return FAILURE_V(makeParseErrorFor<CGCONTRDP>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    Parser localAddrParser(localAddr);
    if (!localAddrParser(&cgcontrdp.localAddr, '/')
                        (&cgcontrdp.localAddrSize).fullMatch()) {
        cgcontrdp.localAddr = std::string(localAddr.data(), localAddr.size());
        cgcontrdp.localAddrSize = 0;
    }

    return make(std::move(cgcontrdp));
}


#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CGFPCCFG"
// 1,5000,32,0,1
AtResponsePtr AtResponse::CGFPCCFG::parse(const std::string_view str) {
    CGFPCCFG cgfpccfg;
    int status;
    int mtech;

    Parser parser(str);
    if (!parser(&status).skip(',')
               (&cgfpccfg.bandwidth).skip(',')
               (&mtech).skip(',')
               (&cgfpccfg.freq).skip(',')
               (&cgfpccfg.contextId).fullMatch()) {
        return FAILURE_V(makeParseErrorFor<CGFPCCFG>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    cgfpccfg.status = static_cast<network::CellConnectionStatus>(status);
    cgfpccfg.mtech = static_cast<ratUtils::ModemTechnology>(mtech);

    return make(std::move(cgfpccfg));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "MBAU"
// <STATUS>[,<KC>,<SRES>][,<CK>,<IK>,<RES/AUTS>]
AtResponsePtr AtResponse::MBAU::parse(const std::string_view str) {
    MBAU mbau;

    std::string_view kc;
    std::string_view sres;
    std::string_view ck;
    std::string_view ik;
    std::string_view resAuts;

    Parser parser(str);
    switch (std::count(str.begin(), str.end(), ',')) {
    default:
failed:
        return FAILURE_V(makeParseErrorFor<MBAU>(),
                         "Can't parse '%*.*s'",
                         int(str.size()), int(str.size()), str.data());

    case 0:
        if (!parser(&mbau.status).fullMatch()) {
            goto failed;
        }
        break;

    case 2:
        if (parser(&mbau.status).skip(',')(&kc, ',').matchSoFar()) {
            sres = parser.remaining();
        } else {
            goto failed;
        }
        break;

    case 5:
        if (parser(&mbau.status).skip(',')(&kc, ',')(&sres, ',')
                  (&ck, ',')(&ik, ',').matchSoFar()) {
            resAuts = parser.remaining();
        } else {
            goto failed;
        }
        break;
    }

    if (!hex2bin(kc, &mbau.kc) || !hex2bin(sres, &mbau.sres) || !hex2bin(ck, &mbau.ck) ||
            !hex2bin(ik, &mbau.ik) || !hex2bin(resAuts, &mbau.resAuts)) {
        goto failed;
    }

    return make(std::move(mbau));
}

#undef FAILURE_DEBUG_PREFIX
#define FAILURE_DEBUG_PREFIX "CTZV"
// 24/11/05:17:01:32-32:0:America!Los_Angeles
AtResponsePtr AtResponse::CTZV::parse(const std::string_view str) {
    int yy, month, day, hh, mm, ss, tzOffset15m;
    char tzSign, daylight;

    Parser parser(str);
    parser.skip(' ')(&yy).skip('/')(&month).skip('/')(&day).skip(':')
          (&hh).skip(':')(&mm).skip(':')(&ss)
          (&tzSign)(&tzOffset15m).skip(':')(&daylight).skip(':');

    if (!parser.matchSoFar()) {
        return FAILURE_V(makeParseErrorFor<CTZV>(),
                         "Can't parse: '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    switch (tzSign) {
    case '+': break;
    case '-': tzOffset15m = -tzOffset15m; break;
    default:
        return FAILURE_V(makeParseErrorFor<CTZV>(),
                         "Unexpected timezone offset sign: '%*.*s'",
                         int(str.size()), int(str.size()), str.data());
    }

    CTZV ctzv = {
        .tzName = toString(parser.remaining()),
        .year = uint16_t(yy + 2000),
        .month = uint8_t(month),
        .day = uint8_t(day),
        .hour = uint8_t(hh),
        .isDaylightSaving = uint8_t(daylight != '0'),
        .minute = uint8_t(mm),
        .second = uint8_t(ss),
        .tzOffset15m = int8_t(tzOffset15m),
    };

    return make(std::move(ctzv));
}

std::string AtResponse::CTZV::nitzString() const {
    return std::format("{:02d}/{:02d}/{:02d}:{:02d}:{:02d}:{:02d}{:+d}:{:d}:{:s}",
                       year % 100, month, day, hour, minute, second,
                       tzOffset15m, isDaylightSaving, tzName.c_str());
}

std::string_view AtResponse::what() const {
    return visitR<std::string_view>(CmdIdVisitor());
}

void AtResponse::unexpected(const char* klass, const char* request,
                            const std::source_location location) const {
    const std::string_view r = what();
    const int rl = r.size();

    LOG_ALWAYS_FATAL("Unexpected response: '%*.*s' in %s:%s at %s:%d in %s", rl, rl, r.data(),
                     klass, request, location.function_name(), location.line(),
                     location.file_name());
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
