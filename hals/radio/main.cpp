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

#include <string_view>
#include <memory>

#include <cutils/properties.h>
#include <fcntl.h>

#include <android/binder_interface_utils.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "AtChannel.h"
#include "ImsMedia.h"
#include "RadioConfig.h"
#include "RadioData.h"
#include "RadioIms.h"
#include "RadioMessaging.h"
#include "RadioModem.h"
#include "RadioNetwork.h"
#include "RadioSim.h"
#include "RadioVoice.h"
#include "Sap.h"

#include "debug.h"

namespace {
using ::android::base::unique_fd;
namespace impl = ::aidl::android::hardware::radio::implementation;

unique_fd openHostChannel(const char propertyName[]) {
    char channelName[PROPERTY_VALUE_MAX];
    if (::property_get(propertyName, channelName, nullptr) <= 0) {
        return FAILURE_V(unique_fd(), "The '%s' property is not defined", propertyName);
    }

    const int fd = ::open(channelName, O_RDWR);
    if (fd >= 0) {
        return unique_fd(fd);
    } else {
        return FAILURE_V(unique_fd(), "Could not open '%s'", channelName);
    }
}

std::string getInstanceName(const std::string_view descriptor,
                            const std::string_view slot) {
    std::string result(descriptor.data(), descriptor.size());
    result.append(1, '/');
    result.append(slot.data(), slot.size());
    return result;
}

template <class S> std::shared_ptr<S> registerService(
        const std::string_view instanceSuffix,
        std::shared_ptr<impl::AtChannel> atChannel) {
    auto serice = ndk::SharedRefBase::make<S>(std::move(atChannel));
    const std::string instanceName = getInstanceName(S::descriptor, instanceSuffix);

    if (AServiceManager_addService(serice->asBinder().get(),
                                   instanceName.c_str()) != STATUS_OK) {
        return FAILURE_V(nullptr, "Failed to register: '%s'",
                         instanceName.c_str());
    }

    return serice;
}

template <class T> void addResponseSink(impl::AtChannel& atChannel,
                                        const std::shared_ptr<T>& strongObject,
                                        void(T::*method)(const impl::AtResponsePtr&)) {
    std::weak_ptr<T> weakObject(strongObject);

    atChannel.addResponseSink([weakObject = std::move(weakObject), method]
                              (const impl::AtResponsePtr& response) -> bool {
        if (const auto strongObject = weakObject.lock()) {
            (*strongObject.*method)(response);
            return true;
        } else {
            return false;
        }
    });
}

int mainImpl(impl::AtChannel::HostChannelFactory hostChannelFactory) {
    using impl::AtChannel;
    using impl::ImsMedia;
    using impl::RadioConfig;
    using impl::RadioData;
    using impl::RadioIms;
    using impl::RadioMessaging;
    using impl::RadioModem;
    using impl::RadioNetwork;
    using impl::RadioSim;
    using impl::RadioVoice;
    using impl::Sap;

    using namespace std::literals;

    auto initSequence = [](const AtChannel::RequestPipe pipe,
                           AtChannel::Conversation& conversation) -> bool {
        static const std::string_view initCmds[] = {
            "ATE0Q0V1"sv,
            "AT+CMEE=1"sv,
            "AT+CREG=2"sv,
            "AT+CGREG=2"sv,
            "AT+CEREG=2"sv,
            "AT+CCWA=1"sv,
            "AT+CMOD=0"sv,
            "AT+CMUT=0"sv,
            "AT+CSSN=0,1"sv,
            "AT+COLP=0"sv,
            "AT+CSCS=\"HEX\""sv,
            "AT+CUSD=1"sv,
            "AT+CGEREP=1,0"sv,
            "AT+CMGF=0"sv,
            "AT+CFUN?"sv,
        };

        for (const std::string_view& cmd : initCmds) {
            using impl::AtResponse;
            using impl::AtResponsePtr;
            using OK = AtResponse::OK;

            const AtResponsePtr response =
                conversation(pipe, cmd,
                             [](const AtResponse& response) -> bool {
                                 return response.holds<OK>();
                             });

            if (!response) {
                return false;
            } else if (!response->isOK()) {
                response->unexpected(__func__, "initSequence");
                return false;
            }
        }

        return true;
    };

    const auto atChannel = std::make_shared<AtChannel>(std::move(hostChannelFactory),
                                                       std::move(initSequence));

    static constexpr std::string_view kDefaultInstance = "default"sv;
    static constexpr std::string_view kSlot1Instance = "slot1"sv;

    ABinderProcess_setThreadPoolMaxThreadCount(2);
    ABinderProcess_startThreadPool();

    const auto imsMedia = registerService<ImsMedia>(kDefaultInstance, atChannel);
    if (!imsMedia) {
        return EXIT_FAILURE;
    }

    const auto radioConfig = registerService<RadioConfig>(kDefaultInstance, atChannel);
    if (!radioConfig) {
        return EXIT_FAILURE;
    }

    const auto radioData = registerService<RadioData>(kSlot1Instance, atChannel);
    if (!radioData) {
        return EXIT_FAILURE;
    }

    const auto radioIms = registerService<RadioIms>(kSlot1Instance, atChannel);
    if (!radioIms) {
        return EXIT_FAILURE;
    }

    const auto radioMessaging = registerService<RadioMessaging>(kSlot1Instance, atChannel);
    if (!radioMessaging) {
        return EXIT_FAILURE;
    }

    const auto radioModem = registerService<RadioModem>(kSlot1Instance, atChannel);
    if (!radioModem) {
        return EXIT_FAILURE;
    }

    const auto radioNetwork = registerService<RadioNetwork>(kSlot1Instance, atChannel);
    if (!radioNetwork) {
        return EXIT_FAILURE;
    }

    const auto radioSim = registerService<RadioSim>(kSlot1Instance, atChannel);
    if (!radioNetwork) {
        return EXIT_FAILURE;
    }

    const auto radioVoice = registerService<RadioVoice>(kSlot1Instance, atChannel);
    if (!radioVoice) {
        return EXIT_FAILURE;
    }

    const auto sap = registerService<Sap>(kSlot1Instance, atChannel);
    if (!sap) {
        return EXIT_FAILURE;
    }

    addResponseSink(*atChannel, imsMedia, &ImsMedia::atResponseSink);
    addResponseSink(*atChannel, radioConfig, &RadioConfig::atResponseSink);
    addResponseSink(*atChannel, radioData, &RadioData::atResponseSink);
    addResponseSink(*atChannel, radioIms, &RadioIms::atResponseSink);
    addResponseSink(*atChannel, radioMessaging, &RadioMessaging::atResponseSink);
    addResponseSink(*atChannel, radioModem, &RadioModem::atResponseSink);
    addResponseSink(*atChannel, radioNetwork, &RadioNetwork::atResponseSink);
    addResponseSink(*atChannel, radioSim, &RadioSim::atResponseSink);
    addResponseSink(*atChannel, radioVoice, &RadioVoice::atResponseSink);
    addResponseSink(*atChannel, sap, &Sap::atResponseSink);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;    // joinThreadPool is not expected to return
}
}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    return mainImpl([](){
        return openHostChannel("vendor.qemu.vport.modem");
    });
}
