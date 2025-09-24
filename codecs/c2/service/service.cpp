// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//#define LOG_NDEBUG 0
#define LOG_TAG "android.hardware.media.c2@1.0-service-goldfish"

#include <C2Component.h>
#include <codec2/hidl/1.0/ComponentStore.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>
#include <minijail.h>

#include <goldfish_codec2/store/GoldfishComponentStore.h>

// Default policy for codec2.0 service.
static constexpr char kBaseSeccompPolicyPath[] =
    "/vendor/etc/seccomp_policy/"
    "android.hardware.media.c2-default-seccomp_policy";

// Additional device-specific seccomp permissions can be added in this file.
static constexpr char kExtSeccompPolicyPath[] =
    "/vendor/etc/seccomp_policy/codec2.vendor.ext.policy";

int main(int /* argc */, char ** /* argv */) {
    signal(SIGPIPE, SIG_IGN);
    android::SetUpMinijail(kBaseSeccompPolicyPath, kExtSeccompPolicyPath);

    android::hardware::configureRpcThreadpool(8, true /* callerWillJoin */);

    using namespace ::android::hardware::media::c2::V1_0;
    const auto store = ::android::sp<utils::ComponentStore>::make(
            android::GoldfishComponentStore::Create());

    if (store->registerAsService("default") != android::OK) {
        ALOGE("Cannot register Codec2's IComponentStore service.");
        return 1;
    }

    android::hardware::joinRpcThreadpool();
    return 0;
}
