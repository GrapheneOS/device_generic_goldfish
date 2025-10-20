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

#define FAILURE_DEBUG_PREFIX "ImsMedia"

#include "ImsMedia.h"

#include <aidl/android/hardware/radio/ims/media/BnImsMediaSession.h>
#include "debug.h"

namespace aidl {
namespace android {
namespace hardware {
namespace radio {
namespace implementation {

using ims::media::IImsMediaSessionListener;
using ims::media::MediaQualityThreshold;
using ims::media::RtpConfig;
using ims::media::RtpError;
using ims::media::RtpHeaderExtension;

namespace {
struct ImsMediaSession : public ims::media::BnImsMediaSession {

    ScopedAStatus setListener(const std::shared_ptr<IImsMediaSessionListener>& sessionListener) override {
        mSessionListener = sessionListener;
        return ScopedAStatus::ok();
    }

    ScopedAStatus modifySession(const RtpConfig& config) override {
        NOT_NULL(mSessionListener)->onModifySessionResponse(config, RtpError::NONE);
        return ScopedAStatus::ok();
    }

    ScopedAStatus sendDtmf(char16_t /*dtmfDigit*/, int32_t /*duration*/) override {
        return ScopedAStatus::ok();
    }

    ScopedAStatus startDtmf(char16_t /*dtmfDigit*/) override {
        return ScopedAStatus::ok();
    }

    ScopedAStatus stopDtmf() override {
        return ScopedAStatus::ok();
    }

    ScopedAStatus sendHeaderExtension(const std::vector<RtpHeaderExtension>& /*extensions*/) override {
        return ScopedAStatus::ok();
    }

    ScopedAStatus setMediaQualityThreshold(const MediaQualityThreshold& /*threshold*/) override {
        return ScopedAStatus::ok();
    }

    ScopedAStatus requestRtpReceptionStats(int32_t /*intervalMs*/) override {
        return ScopedAStatus::ok();
    }

    ScopedAStatus adjustDelay(int32_t /*delayMs*/) override {
        return ScopedAStatus::ok();
    }

    std::shared_ptr<IImsMediaSessionListener> mSessionListener;
};
}  // namespace

ImsMedia::ImsMedia(std::shared_ptr<AtChannel> /*atChannel*/) {
}

ScopedAStatus ImsMedia::openSession(
        const int32_t sessionId, const ims::media::LocalEndPoint& /*localEndPoint*/,
        const ims::media::RtpConfig& /*config*/) {
    NOT_NULL(mMediaListener)->onOpenSessionSuccess(
        sessionId, ndk::SharedRefBase::make<ImsMediaSession>());
    return ScopedAStatus::ok();
}

ScopedAStatus ImsMedia::closeSession(const int32_t sessionId) {
    NOT_NULL(mMediaListener)->onSessionClosed(sessionId);
    return ScopedAStatus::ok();
}

void ImsMedia::atResponseSink(const AtResponsePtr& /*response*/) {}

ScopedAStatus ImsMedia::setListener(
        const std::shared_ptr<ims::media::IImsMediaListener>& mediaListener) {
    mMediaListener = NOT_NULL(mediaListener);
    return ScopedAStatus::ok();
}

}  // namespace implementation
}  // namespace radio
}  // namespace hardware
}  // namespace android
}  // namespace aidl
