/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <log/log.h>
#include "device_port_sink.h"
#include "talsa.h"
#include "util.h"

namespace android {
namespace hardware {
namespace audio {
namespace V6_0 {
namespace implementation {

namespace {

struct TinyalsaSink : public DevicePortSink {
    TinyalsaSink(unsigned pcmCard, unsigned pcmDevice, const AudioConfig &cfg)
            : mSampleRateHz(cfg.sampleRateHz)
            , mPcm(talsa::pcmOpen(pcmCard, pcmDevice,
                                  util::countChannels(cfg.channelMask),
                                  cfg.sampleRateHz,
                                  cfg.frameCount,
                                  true /* isOut */)) {}

    Result getPresentationPosition(uint64_t &frames, TimeSpec &ts) const override {
        nsecs_t t = 0;
        mPos.now(mSampleRateHz, frames, t);
        ts.tvSec = ns2s(t);
        ts.tvNSec = t - s2ns(ts.tvSec);
        return Result::OK;
    }

    int write(const void *data, size_t nBytes) override {
        const int res = ::pcm_write(mPcm.get(), data, nBytes);
        if (res < 0) {
            return res;
        } else if (res == 0) {
            mPos.addFrames(::pcm_bytes_to_frames(mPcm.get(), nBytes));
            return nBytes;
        } else {
            mPos.addFrames(::pcm_bytes_to_frames(mPcm.get(), res));
            return res;
        }
    }

    static std::unique_ptr<TinyalsaSink> create(unsigned pcmCard,
                                                unsigned pcmDevice,
                                                const AudioConfig &cfg) {
        auto src = std::make_unique<TinyalsaSink>(pcmCard, pcmDevice, cfg);
        if (src->mPcm) {
            return src;
        } else {
            return nullptr;
        }
    }

private:
    const uint32_t mSampleRateHz;
    util::StreamPosition mPos;
    talsa::PcmPtr mPcm;
};

struct NullSink : public DevicePortSink {
    explicit NullSink(const AudioConfig &cfg)
            : mSampleRateHz(cfg.sampleRateHz)
            , mNChannels(util::countChannels(cfg.channelMask)) {}

    Result getPresentationPosition(uint64_t &frames, TimeSpec &ts) const override {
        nsecs_t t = 0;
        mPos.now(mSampleRateHz, frames, t);
        ts.tvSec = ns2s(t);
        ts.tvNSec = t - s2ns(ts.tvSec);
        return Result::OK;
    }

    int write(const void *, size_t nBytes) override {
        mPos.addFrames(nBytes / mNChannels / sizeof(int16_t));
        return nBytes;
    }

    static std::unique_ptr<NullSink> create(const AudioConfig &cfg) {
        return std::make_unique<NullSink>(cfg);
    }

private:
    const unsigned mSampleRateHz;
    const unsigned mNChannels;
    util::StreamPosition mPos;
};

}  // namespace

std::unique_ptr<DevicePortSink>
DevicePortSink::create(const DeviceAddress &address,
                       const AudioConfig &cfg,
                       const hidl_bitfield<AudioOutputFlag> &flags) {
    (void)flags;

    if (cfg.format != AudioFormat::PCM_16_BIT) {
        ALOGE("%s:%d Only PCM_16_BIT is supported", __func__, __LINE__);
        return nullptr;
    }

    switch (address.device) {
    case AudioDevice::OUT_SPEAKER:
        return TinyalsaSink::create(talsa::kPcmCard, talsa::kPcmDevice, cfg);

    case AudioDevice::OUT_TELEPHONY_TX:
        return NullSink::create(cfg);

    default:
        return nullptr;
    }
}

}  // namespace implementation
}  // namespace V6_0
}  // namespace audio
}  // namespace hardware
}  // namespace android
