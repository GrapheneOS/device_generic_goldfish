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

#include <cmath>
#include <chrono>
#include <thread>
#include <audio_utils/channels.h>
#include <audio_utils/format.h>
#include <log/log.h>
#include <utils/Timers.h>
#include "device_port_source.h"
#include "talsa.h"
#include "util.h"
#include "debug.h"

namespace android {
namespace hardware {
namespace audio {
namespace V6_0 {
namespace implementation {

namespace {

struct CapturePositionDevicePortSource : public DevicePortSource {
    explicit CapturePositionDevicePortSource(unsigned sampleRateHz)
            : mSampleRateHz(sampleRateHz)
            , mStartNs(systemTime(SYSTEM_TIME_MONOTONIC)) {}

    Result getCapturePosition(uint64_t &frames, uint64_t &time) const override {
        const nsecs_t nowNs = systemTime(SYSTEM_TIME_MONOTONIC);
        frames = getNowFrames(nowNs);
        time = uint64_t(nowNs);
        return Result::OK;
    }

    uint64_t getNowFrames(const nsecs_t nowNs) const {
        return uint64_t(mSampleRateHz) * ns2ms(nowNs - mStartNs) / 1000;
    }

protected:
    const unsigned mSampleRateHz;
    const nsecs_t mStartNs;
};

struct TinyalsaSource : public CapturePositionDevicePortSource {
    TinyalsaSource(unsigned pcmCard, unsigned pcmDevice, const AudioConfig &cfg)
            : CapturePositionDevicePortSource(cfg.sampleRateHz)
            , pcm(talsa::pcmOpen(pcmCard, pcmDevice,
                                 util::countChannels(cfg.channelMask),
                                 cfg.sampleRateHz,
                                 cfg.frameCount,
                                 false /* isOut */)) {}

    int read(void *data, size_t toReadBytes) override {
        const int res = ::pcm_read(pcm.get(), data, toReadBytes);
        if (res < 0) {
            return FAILURE(res);
        } else if (res == 0) {
            return toReadBytes;
        } else {
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
            return FAILURE(nullptr);
        }
    }

private:
    talsa::PcmPtr pcm;
};

template <class G> struct GeneratedSource : public CapturePositionDevicePortSource {
    GeneratedSource(const AudioConfig &cfg, G generator)
            : CapturePositionDevicePortSource(cfg.sampleRateHz)
            , mNChannels(util::countChannels(cfg.channelMask))
            , mGenerator(std::move(generator)) {}

    int read(void *data, size_t toReadBytes) override {
        int16_t *samples = static_cast<int16_t *>(data);
        const unsigned nChannels = mNChannels;
        const unsigned requestedFrames = toReadBytes / nChannels / sizeof(*samples);

        unsigned availableFrames;
        while (true) {
            const nsecs_t nowNs = systemTime(SYSTEM_TIME_MONOTONIC);
            availableFrames = getNowFrames(nowNs) - mSentFrames;
            if (availableFrames < requestedFrames / 2) {
                const unsigned neededMoreFrames = requestedFrames / 2 - availableFrames;

                using namespace std::chrono_literals;
                std::this_thread::sleep_for(1s * neededMoreFrames / mSampleRateHz);
            } else {
                break;
            }
        }

        const unsigned nFrames = std::min(requestedFrames, availableFrames);
        mGenerator(samples, nFrames);
        const size_t sizeBytes = nFrames * nChannels * sizeof(*samples);
        if (nChannels > 1) {
            adjust_channels(samples, 1, samples, nChannels,
                            sizeof(*samples), sizeBytes);
        }

        mSentFrames += nFrames;
        return sizeBytes;
    }

private:
    const unsigned mNChannels;
    uint64_t mSentFrames = 0;
    G mGenerator;
};

std::vector<int16_t> convertFloatsToInt16(const std::vector<float> &pcmFloat) {
    std::vector<int16_t> pcmI16(pcmFloat.size());

    memcpy_by_audio_format(pcmI16.data(),   AUDIO_FORMAT_PCM_16_BIT,
                           pcmFloat.data(), AUDIO_FORMAT_PCM_FLOAT,
                           pcmFloat.size());

    return pcmI16;
}

// https://en.wikipedia.org/wiki/Busy_signal
struct BusySignalGenerator {
    explicit BusySignalGenerator(const uint32_t sampleRateHz) : mSampleRateHz(sampleRateHz) {
        // 24/480 = 31/620, mValues must contain 50ms of audio samples
        const size_t sz = sampleRateHz / 20;
        std::vector<float> pcm(sz);
        for (unsigned i = 0; i < sz; ++i) {
            const double a = double(i) * M_PI * 2 / sampleRateHz;
            pcm[i] = .5 * (sin(480 * a) + sin(620 * a));
        }
        mValues = convertFloatsToInt16(pcm);
    }

    void operator()(int16_t* s, size_t n) {
        const unsigned rate = mSampleRateHz;
        const unsigned rateHalf = rate / 2;
        const int16_t *const vals = mValues.data();
        const size_t valsSz = mValues.size();
        size_t i = mI;

        while (n > 0) {
            size_t len;
            if (i < rateHalf) {
                const size_t valsOff = i % valsSz;
                len = std::min(n, std::min(rateHalf - i, valsSz - valsOff));
                memcpy(s, vals + valsOff, len * sizeof(*s));
            } else {
                len = std::min(n, rate - i);
                memset(s, 0, len * sizeof(*s));
            }
            s += len;
            i = (i + len) % rate;
            n -= len;
        }

        mI = i;
    }

private:
    const unsigned mSampleRateHz;
    std::vector<int16_t> mValues;
    size_t mI = 0;
};

struct RepeatGenerator {
    explicit RepeatGenerator(const std::vector<float> &pcm)
            : mValues(convertFloatsToInt16(pcm)) {}

    void operator()(int16_t* s, size_t n) {
        const int16_t *const vals = mValues.data();
        const size_t valsSz = mValues.size();
        size_t i = mI;

        while (n > 0) {
            const size_t len = std::min(n, valsSz - i);
            memcpy(s, vals + i, len * sizeof(*s));
            s += len;
            i = (i + len) % valsSz;
            n -= len;
        }

        mI = i;
    }

private:
    const std::vector<int16_t> mValues;
    size_t mI = 0;
};

std::vector<float> generateSinePattern(uint32_t sampleRateHz,
                                       double freq,
                                       double amp) {
    std::vector<float> result(3 * sampleRateHz / freq + .5);

    for (size_t i = 0; i < result.size(); ++i) {
        const double a = double(i) * M_PI * 2 / sampleRateHz;
        result[i] = amp * sin(a * freq);
    }

    return result;
}

template <class G> std::unique_ptr<GeneratedSource<G>>
createGeneratedSource(const AudioConfig &cfg, G generator) {
    return std::make_unique<GeneratedSource<G>>(cfg, std::move(generator));
}

}  // namespace

std::unique_ptr<DevicePortSource>
DevicePortSource::create(const DeviceAddress &address,
                         const AudioConfig &cfg,
                         const hidl_bitfield<AudioOutputFlag> &flags) {
    (void)flags;

    if (cfg.format != AudioFormat::PCM_16_BIT) {
        ALOGE("%s:%d Only PCM_16_BIT is supported", __func__, __LINE__);
        return FAILURE(nullptr);
    }

    switch (address.device) {
    case AudioDevice::IN_BUILTIN_MIC:
        return TinyalsaSource::create(talsa::kPcmCard, talsa::kPcmDevice, cfg);

    case AudioDevice::IN_TELEPHONY_RX:
        return createGeneratedSource(cfg, BusySignalGenerator(cfg.sampleRateHz));

    case AudioDevice::IN_FM_TUNER:
        return createGeneratedSource(
            cfg, RepeatGenerator(generateSinePattern(cfg.sampleRateHz, 440.0, 1.0)));

    default:
        return FAILURE(nullptr);
    }
}

}  // namespace implementation
}  // namespace V6_0
}  // namespace audio
}  // namespace hardware
}  // namespace android
