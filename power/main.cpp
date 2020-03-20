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

#define LOG_TAG "android.hardware.power@1.3-service.ranchu"
#include <android/log.h>
#include <android/hardware/power/1.3/IPower.h>
#include <hidl/LegacySupport.h>

namespace {
namespace ahp = ::android::hardware::power;
typedef ahp::V1_0::Status Status0;
typedef ahp::V1_0::PowerStatePlatformSleepState PowerStatePlatformSleepState0;
typedef ahp::V1_1::PowerStateSubsystem PowerStateSubsystem1;
typedef ahp::V1_1::PowerStateSubsystemSleepState PowerStateSubsystemSleepState1;

using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;

enum class SubsystemType {
    Wlan,
    //Don't add any lines after that line
    Count
};
enum class WlanParamId {
    CumulativeSleepTimeMs,
    CumulativeTotalTimeOnMs,
    DeepSleepEnterCounter,
    LastDeepSleepEnterTstampMs,
    //Don't add any lines after that line
    ParamCount
};
enum WlanStateId {
    Active = 0,
    DeepSleep,
    //Don't add any lines after that line
    Count
};

struct Power3 : public ahp::V1_3::IPower {
    Power3() {
    }

    // v1.3
    Return<void> powerHintAsync_1_3(ahp::V1_3::PowerHint hint, int32_t data) override {
        (void)hint;
        (void)data;
        return {};
    }

    // v1.2
    Return<void> powerHintAsync_1_2(ahp::V1_2::PowerHint hint, int32_t data) override {
        (void)hint;
        (void)data;
        return {};
    }

    // v1.1
    Return<void> getSubsystemLowPowerStats(getSubsystemLowPowerStats_cb _hidl_cb) override {
        hidl_vec<PowerStateSubsystem1> subsystems;
        subsystems.resize(static_cast<size_t>(SubsystemType::Count));

        getWlanLowPowerStats(&subsystems[static_cast<size_t>(SubsystemType::Wlan)]);

        _hidl_cb(subsystems, Status0::SUCCESS);
        return {};
    }

    Return<void> powerHintAsync(ahp::V1_0::PowerHint hint, int32_t data) override {
        return powerHint(hint, data);
    }

    // v1.0
    Return<void> setInteractive(bool interactive) override {
        (void)interactive;
        return {};
    }

    Return<void> powerHint(ahp::V1_0::PowerHint hint, int32_t data) override {
        (void)hint;
        (void)data;
        return {};
    }

    Return<void> setFeature(ahp::V1_0::Feature feature, bool activate) override {
        (void)feature;
        (void)activate;
        return {};
    }

    Return<void> getPlatformLowPowerStats(getPlatformLowPowerStats_cb _hidl_cb) override {
        hidl_vec<PowerStatePlatformSleepState0> states;
        states.resize(0);
        _hidl_cb(states, Status0::SUCCESS);
        return {};
    }

    static void getWlanLowPowerStats(PowerStateSubsystem1* subsystem) {
        subsystem->name = "wlan";
        subsystem->states.resize(static_cast<size_t>(WlanStateId::Count));

        PowerStateSubsystemSleepState1* state;

        /* Update statistics for Active State */
        state = &subsystem->states[static_cast<size_t>(WlanStateId::Active)];
        state->name = "Active";
        state->residencyInMsecSinceBoot = 1000;
        state->totalTransitions = 1;
        state->lastEntryTimestampMs = 0;
        state->supportedOnlyInSuspend = false;

        /* Update statistics for Deep-Sleep state */
        state = &subsystem->states[static_cast<size_t>(WlanStateId::DeepSleep)];
        state->name = "Deep-Sleep";
        state->residencyInMsecSinceBoot = 0;
        state->totalTransitions = 0;
        state->lastEntryTimestampMs = 0;
        state->supportedOnlyInSuspend = false;
    }
};
}  // namespace

int main(int, char**) {
    using ::android::sp;
    ::android::hardware::configureRpcThreadpool(1, true /* callerWillJoin */);

    sp<ahp::V1_3::IPower> power(new Power3());
    if (power->registerAsService() != ::android::NO_ERROR) {
        ALOGE("failed to register the IPower@1.3 service");
        return -EINVAL;
    }

    ALOGI("IPower@1.3 service is initialized");

    ::android::hardware::joinRpcThreadpool();
    ALOGI("IPower@1.3 service is terminating");
    return 0;
}
