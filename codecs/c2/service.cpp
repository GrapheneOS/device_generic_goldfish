// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <codec2/aidl/ComponentStore.h>
#include <log/log.h>
#include <minijail.h>

#include <debug.h>
#include "GoldfishComponentStore.h"

// Default policy for codec2.0 service.
static constexpr char kBaseSeccompPolicyPath[] =
    "/vendor/etc/seccomp_policy/"
    "android.hardware.media.c2-default-seccomp_policy";

// Additional device-specific seccomp permissions can be added in this file.
static constexpr char kExtSeccompPolicyPath[] =
    "/vendor/etc/seccomp_policy/codec2.vendor.ext.policy";

int main(int /* argc */, char ** /* argv */) {
    using aidl::android::hardware::media::c2::utils::ComponentStore;

    signal(SIGPIPE, SIG_IGN);
    android::SetUpMinijail(kBaseSeccompPolicyPath, kExtSeccompPolicyPath);

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    const auto cs = ndk::SharedRefBase::make<ComponentStore>(
            std::make_shared<goldfish::media::c2::GoldfishComponentStore>());

    {
        const std::string instance = std::string(ComponentStore::descriptor) + "/default";

        if (AServiceManager_addService(cs->asBinder().get(),
                                       instance.c_str()) != STATUS_OK) {
            return FAILURE_V(android::NO_INIT,
                             "Could not register '%s'", instance.c_str());
        }
    }

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // never exits
}
