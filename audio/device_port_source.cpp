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

#include "device_port_source.h"
#include "talsa.h"
#include "util.h"
#include <cmath>

namespace android {
namespace hardware {
namespace audio {
namespace V6_0 {
namespace implementation {

namespace {

struct TinyalsaSource : public DevicePortSource {
    TinyalsaSource(unsigned pcmCard, unsigned pcmDevice, const AudioConfig &cfg)
            : sampleRateHz(cfg.sampleRateHz)
            , pcm(talsa::pcmOpen(pcmCard, pcmDevice,
                                 util::countChannels(cfg.channelMask),
                                 cfg.sampleRateHz,
                                 cfg.frameCount,
                                 false /* isOut */)) {}

    Result getCapturePosition(uint64_t &frames, uint64_t &time) const override {
        nsecs_t t = 0;
        pos.now(sampleRateHz, frames, t);
        time = t;
        return Result::OK;
    }

    int read(void *data, size_t toReadBytes) override {
        const int res = ::pcm_read(pcm.get(), data, toReadBytes);
        if (res < 0) {
            return res;
        } else if (res == 0) {
            pos.addFrames(::pcm_bytes_to_frames(pcm.get(), toReadBytes));
            return toReadBytes;
        } else {
            pos.addFrames(::pcm_bytes_to_frames(pcm.get(), res));
            return res;
        }
    }

    static std::unique_ptr<TinyalsaSource> create(unsigned pcmCard,
                                                  unsigned pcmDevice,
                                                  const AudioConfig &cfg) {
        auto src = std::make_unique<TinyalsaSource>(pcmCard, pcmDevice, cfg);
        if (src->pcm) {
            return src;
        } else {
            return nullptr;
        }
    }

    const uint32_t sampleRateHz;
    util::StreamPosition pos;
    talsa::PcmPtr pcm;
};

}  // namespace

std::unique_ptr<DevicePortSource>
DevicePortSource::create(const DeviceAddress &address,
                         const AudioConfig &cfg,
                         const hidl_bitfield<AudioOutputFlag> &flags) {
    (void)address;
    (void)flags;
    return TinyalsaSource::create(talsa::kPcmCard, talsa::kPcmDevice, cfg);
}

}  // namespace implementation
}  // namespace V6_0
}  // namespace audio
}  // namespace hardware
}  // namespace android
