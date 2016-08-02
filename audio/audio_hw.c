/*
 * Copyright (C) 2012 The Android Open Source Project
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

#define LOG_TAG "audio_hw_generic"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <fcntl.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <tinyalsa/asoundlib.h>

#define PCM_CARD 0
#define PCM_DEVICE 0


#define OUT_PERIOD_SIZE 512
#define OUT_LONG_PERIOD_COUNT 2

#define IN_PERIOD_MS 20
#define IN_PERIOD_COUNT 4


struct generic_audio_device {
    struct audio_hw_device device; // Constant after init
    pthread_mutex_t lock;
    bool mic_mute;                 // Proteced by this->lock
};

/* If not NULL, this is a pointer to the fallback module.
 * This really is the original goldfish audio device /dev/eac which we will use
 * if no alsa devices are detected.
 */
static struct audio_module*  sFallback;
static pthread_once_t sFallbackOnce = PTHREAD_ONCE_INIT;
static void fallback_init(void);
static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state);

struct generic_stream_out {
    struct audio_stream_out stream;   // Constant after init
    pthread_mutex_t lock;
    struct generic_audio_device *dev; // Constant after init
    audio_devices_t device;           // Protected by this->lock
    struct audio_config req_config;   // Constant after init
    struct pcm *pcm;                  // Protected by this->lock
    struct pcm_config pcm_config;     // Constant after init
};

struct generic_stream_in {
    struct audio_stream_in stream;    // Constant after init
    pthread_mutex_t lock;
    struct generic_audio_device *dev; // Constant after init
    audio_devices_t device;           // Protected by this->lock
    struct audio_config req_config;   // Constant after init
    struct pcm *pcm;                  // Protecetd by this->lock
    struct pcm_config pcm_config;     // Constant after init
    int16_t *stereo_to_mono_buf;      // Protected by this->lock
    size_t stereo_to_mono_buf_size;   // Protected by this->lock
};

static struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = 0,
    .period_size = OUT_PERIOD_SIZE,
    .period_count = OUT_LONG_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
};

static struct pcm_config pcm_config_in = {
    .channels = 2,
    .rate = 0,
    .period_size = 0,
    .period_count = IN_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
};

static pthread_mutex_t adev_init_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int audio_device_ref_count = 0;

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    return out->req_config.sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    int channel_count = popcount(out->req_config.channel_mask);
    int size = out->pcm_config.period_size *
                audio_stream_out_frame_size(&out->stream);

    return size;
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    return out->req_config.channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    return out->req_config.format;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}


static void do_out_standby(struct generic_stream_out *out)
{
    pthread_mutex_lock(&out->lock);
    if (out->pcm) {
        pcm_close(out->pcm); // Frees out->pcm
        out->pcm = NULL;
    }
    pthread_mutex_unlock(&out->lock);
}

static int out_standby(struct audio_stream *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    do_out_standby(out);
    return 0;
}

static void do_in_standby(struct generic_stream_in *in)
{
    pthread_mutex_lock(&in->lock);
    if (in->pcm) {
        pcm_close(in->pcm); // Frees in->pcm
        in->pcm = NULL;
    }
    pthread_mutex_unlock(&in->lock);
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    pthread_mutex_lock(&out->lock);
    dprintf(fd, "\tout_dump:\n"
                "\t\tsample rate: %u\n"
                "\t\tbuffer size: %u\n"
                "\t\tchannel mask: %08x\n"
                "\t\tformat: %d\n"
                "\t\tdevice: %08x\n"
                "\t\taudio dev: %p\n\n",
                out_get_sample_rate(stream),
                out_get_buffer_size(stream),
                out_get_channels(stream),
                out_get_format(stream),
                out->device,
                out->dev);
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    struct str_parms *parms;
    char value[32];
    int ret;
    long val;
    char *end;

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                            value, sizeof(value));
    if (ret >= 0) {
        errno = 0;
        val = strtol(value, &end, 10);
        if (errno == 0 && (end != NULL) && (*end == '\0') && ((int)val == val)) {
            pthread_mutex_lock(&out->lock);
            out->device = (int)val;
            pthread_mutex_unlock(&out->lock);
        } else {
            ret = -EINVAL;
        }
    }

    str_parms_destroy(parms);
    return ret;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    char *str;
    char value[256];
    struct str_parms *reply = str_parms_create();
    int ret;

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        pthread_mutex_lock(&out->lock);
        str_parms_add_int(reply, AUDIO_PARAMETER_STREAM_ROUTING, out->device);
        pthread_mutex_unlock(&out->lock);
        str = strdup(str_parms_to_str(reply));
    } else {
        str = strdup(keys);
    }

    str_parms_destroy(query);
    str_parms_destroy(reply);
    return str;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    return (out->pcm_config.period_size *
            out->pcm_config.period_count * 1000) / out->pcm_config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    return -ENOSYS;
}


/*
 * start_output_stream must be called with out->lock held.
 */
static int start_output_stream(struct generic_stream_out *out)
{
    if (out->pcm) {
        ALOGE("pcm_open(out) failed: already open");
        return -ENOSYS;
    }
    // pcm_open always returns a non-null pcm ptr which must be
    // checked with pcm_is_ready
    out->pcm = pcm_open(PCM_CARD, PCM_DEVICE, PCM_OUT, &out->pcm_config);
    if (!pcm_is_ready(out->pcm)) {
        ALOGE("pcm_open(out) failed: %s: channels %d format %d rate %d",
              pcm_get_error(out->pcm),
              out->pcm_config.channels,
              out->pcm_config.format,
              out->pcm_config.rate
              );
        return -ENOMEM;
    }
    return 0;
}

/*
 * start_input_stream must be called with in->lock held.
 */
static int start_input_stream(struct generic_stream_in *in)
{
    if (in->pcm) {
        ALOGE("pcm_open(in) failed: already open");
        return -ENOSYS;
    }
    // pcm_open always returns a non-null pcm ptr which must be
    // checked with pcm_is_ready
    in->pcm = pcm_open(PCM_CARD, PCM_DEVICE, PCM_IN, &in->pcm_config);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("pcm_open(in) failed: %s: channels %d format %d rate %d",
              pcm_get_error(in->pcm),
              in->pcm_config.channels,
              in->pcm_config.format,
              in->pcm_config.rate
              );
        return -ENOMEM;
    }
    return 0;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret = 0;
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    pthread_mutex_lock(&out->lock);
    if (!out->pcm) {
        ret = start_output_stream(out);
    }
    if (ret == 0) {
        ret = pcm_write(out->pcm, buffer, bytes);
    }
    pthread_mutex_unlock(&out->lock);

    if (ret != 0)
        bytes = 0;
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    return -ENOSYS;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    // out_add_audio_effect is a no op
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    // out_remove_audio_effect is a no op
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    return -ENOSYS;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    return in->req_config.sample_rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static int refine_output_parameters(uint32_t *sample_rate, audio_format_t *format, audio_channel_mask_t *channel_mask)
{
    static const uint32_t sample_rates [] = {44100};
    static const int sample_rates_count = sizeof(sample_rates)/sizeof(uint32_t);
    bool inval = false;
    if (*format != AUDIO_FORMAT_PCM_16_BIT) {
        *format = AUDIO_FORMAT_PCM_16_BIT;
        inval = true;
    }

    int channel_count = popcount(*channel_mask);
    if (channel_count != 2) {
        *channel_mask = AUDIO_CHANNEL_IN_STEREO;
        inval = true;
    }

    int i;
    for (i = 0; i < sample_rates_count; i++) {
        if (*sample_rate < sample_rates[i]) {
            *sample_rate = sample_rates[i];
            inval=true;
            break;
        }
        else if (*sample_rate == sample_rates[i]) {
            break;
        }
        else if (i == sample_rates_count-1) {
            // Cap it to the highest rate we support
            *sample_rate = sample_rates[i];
            inval=true;
        }
    }

    if (inval) {
        return -EINVAL;
    }
    return 0;
}

static int check_output_parameters(uint32_t sample_rate, audio_format_t format,
                                  audio_channel_mask_t channel_mask)
{
    return refine_output_parameters(&sample_rate, &format, &channel_mask);
}


static int refine_input_parameters(uint32_t *sample_rate, audio_format_t *format, audio_channel_mask_t *channel_mask)
{
    static const uint32_t sample_rates [] = {8000, 11025, 16000, 22050, 44100, 48000};
    static const int sample_rates_count = sizeof(sample_rates)/sizeof(uint32_t);
    bool inval = false;
    // Only PCM_16_bit is supported. If this is changed, stereo to mono drop
    // must be fixed in in_read
    if (*format != AUDIO_FORMAT_PCM_16_BIT) {
        *format = AUDIO_FORMAT_PCM_16_BIT;
        inval = true;
    }

    int channel_count = popcount(*channel_mask);
    if (channel_count != 1 && channel_count != 2) {
        *channel_mask = AUDIO_CHANNEL_IN_STEREO;
        inval = true;
    }

    int i;
    for (i = 0; i < sample_rates_count; i++) {
        if (*sample_rate < sample_rates[i]) {
            *sample_rate = sample_rates[i];
            inval=true;
            break;
        }
        else if (*sample_rate == sample_rates[i]) {
            break;
        }
        else if (i == sample_rates_count-1) {
            // Cap it to the highest rate we support
            *sample_rate = sample_rates[i];
            inval=true;
        }
    }

    if (inval) {
        return -EINVAL;
    }
    return 0;
}

static int check_input_parameters(uint32_t sample_rate, audio_format_t format,
                                  audio_channel_mask_t channel_mask)
{
    return refine_input_parameters(&sample_rate, &format, &channel_mask);
}

static size_t get_input_buffer_size(uint32_t sample_rate, audio_format_t format,
                                    audio_channel_mask_t channel_mask)
{
    size_t size;
    size_t device_rate;
    int channel_count = popcount(channel_mask);
    if (check_input_parameters(sample_rate, format, channel_count) != 0)
        return 0;

    size = sample_rate*IN_PERIOD_MS/1000;
    // Audioflinger expects audio buffers to be multiple of 16 frames
    size = ((size + 15) / 16) * 16;
    size *= sizeof(short) * channel_count;

    return size;
}


static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    int size = get_input_buffer_size(in->req_config.sample_rate,
                                 in->req_config.format,
                                 in->req_config.channel_mask);

    return size;
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    return in->req_config.channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    return in->req_config.format;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static int in_standby(struct audio_stream *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    do_in_standby(in);
    return 0;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;

    pthread_mutex_lock(&in->lock);
    dprintf(fd, "\tin_dump:\n"
                "\t\tsample rate: %u\n"
                "\t\tbuffer size: %u\n"
                "\t\tchannel mask: %08x\n"
                "\t\tformat: %d\n"
                "\t\tdevice: %08x\n"
                "\t\taudio dev: %p\n\n",
                in_get_sample_rate(stream),
                in_get_buffer_size(stream),
                in_get_channels(stream),
                in_get_format(stream),
                in->device,
                in->dev);
    pthread_mutex_unlock(&in->lock);
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    struct str_parms *parms;
    char value[32];
    int ret;
    long val;
    char *end;

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                            value, sizeof(value));
    if (ret >= 0) {
        errno = 0;
        val = strtol(value, &end, 10);
        if ((errno == 0) && (end != NULL) && (*end == '\0') && ((int)val == val)) {
            in->device = (int)val;
        } else {
            ret = -EINVAL;
        }
    }

    str_parms_destroy(parms);
    return ret;
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    char *str;
    char value[256];
    struct str_parms *reply = str_parms_create();
    int ret;

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        str_parms_add_int(reply, AUDIO_PARAMETER_STREAM_ROUTING, in->device);
        str = strdup(str_parms_to_str(reply));
    } else {
        str = strdup(keys);
    }

    str_parms_destroy(query);
    str_parms_destroy(reply);
    return str;
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    // in_set_gain is a no op
    return 0;
}


static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    struct generic_audio_device *adev = in->dev;
    int ret = 0;
    bool mic_mute;

    adev_get_mic_mute(&adev->device, &mic_mute);

    pthread_mutex_lock(&in->lock);

    if (mic_mute) {
        goto exit;
    }

    if (!in->pcm) {
        ret = start_input_stream(in);
    }
    if (ret < 0)
        goto exit;

    if (ret == 0) {
        if (popcount(in->req_config.channel_mask) == 1 &&
            in->pcm_config.channels == 2) {
            // Need to resample to mono
            if (in->stereo_to_mono_buf_size < bytes) {
                in->stereo_to_mono_buf = realloc(in->stereo_to_mono_buf,
                                                 bytes);
                if (!in->stereo_to_mono_buf) {
                    ALOGE("Failed to allocate stereo_to_mono_buff");
                    ret = -ENOMEM;
                    goto exit;
                }
            }
            ret = pcm_read(in->pcm, in->stereo_to_mono_buf, bytes);
            if (ret != 0) {
                goto exit;
            }

            // Currently only pcm 16 is supported.
            uint16_t *src = (uint16_t *)in->stereo_to_mono_buf;
            uint16_t *dst = (uint16_t *)buffer;
            size_t i;
            bytes = bytes/2;
            // Resample stereo 16 to mono 16 by dropping one channel.
            // The stereo stream is interleaved L-R-L-R
            for (i = 0; i < bytes; i++) {
                *dst=*src;
                src+=2;
                dst+=1;
            }
            goto exit;
        } else {
            ret = pcm_read(in->pcm, buffer, bytes);
            if (ret != 0) {
                goto exit;
            }
        }
    }

exit:
    pthread_mutex_unlock(&in->lock);
    if (ret != 0 || mic_mute) {
        // On any read error / muted, just set buffer to 0 and sleep for
        // expected amount of time.
        memset(buffer, 0, bytes);
        usleep(bytes * 1000 * 1000 / audio_stream_in_frame_size(&in->stream) /
               in_get_sample_rate(&stream->common));
    }
    return bytes;

}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    // in_add_audio_effect is a no op
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    // in_add_audio_effect is a no op
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address __unused)
{
    struct generic_audio_device *adev = (struct generic_audio_device *)dev;
    struct generic_stream_out *out;
    int ret = 0;

    if (refine_output_parameters(&config->sample_rate, &config->format, &config->channel_mask)) {
        ALOGE("Error opening output stream format %d, channel_mask %04x, sample_rate %u",
              config->format, config->channel_mask, config->sample_rate);
        ret = -EINVAL;
        goto error;
    }

    out = (struct generic_stream_out *)calloc(1, sizeof(struct generic_stream_out));
    if (!out)
        return -ENOMEM;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;

    out->dev = adev;
    out->device = devices;
    pthread_mutex_init(&out->lock, (const pthread_mutexattr_t *) NULL);

    memcpy(&out->req_config, config, sizeof(struct audio_config));

    memcpy(&out->pcm_config, &pcm_config_out, sizeof(struct pcm_config));
    out->pcm_config.rate = config->sample_rate;

    *stream_out = &out->stream;

    ret = start_output_stream(out);

error:

    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    do_out_standby(out);
    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    return 0;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    // adev_set_voice_volume is a no op (simulates phones)
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev, bool muted)
{
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev, bool *muted)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    // adev_set_mode is a no op (simulates phones)
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct generic_audio_device *adev = (struct generic_audio_device *)dev;
    pthread_mutex_lock(&adev->lock);
    adev->mic_mute = state;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct generic_audio_device *adev = (struct generic_audio_device *)dev;
    pthread_mutex_lock(&adev->lock);
    *state = adev->mic_mute;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}


static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    size_t size;
    int channel_count = popcount(config->channel_mask);
    return get_input_buffer_size(config->sample_rate, config->format, channel_count);
}


static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    do_in_standby(in);
    if (in->stereo_to_mono_buf != NULL) {
        free(in->stereo_to_mono_buf);
        in->stereo_to_mono_buf_size = 0;
    }
    free(stream);
}


static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags __unused,
                                  const char *address __unused,
                                  audio_source_t source __unused)
{
    struct generic_audio_device *adev = (struct generic_audio_device *)dev;
    struct generic_stream_in *in;
    int ret = 0;
    if (refine_input_parameters(&config->sample_rate, &config->format, &config->channel_mask)) {
        ALOGE("Error opening input stream format %d, channel_mask %04x, sample_rate %u",
              config->format, config->channel_mask, config->sample_rate);
        ret = -EINVAL;
        goto error;
    }

    in = (struct generic_stream_in *)calloc(1, sizeof(struct generic_stream_in));
    if (!in) {
        ret = -ENOMEM;
        goto error;
    }

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;         // no op
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;                   // no op
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;       // no op
    in->stream.common.remove_audio_effect = in_remove_audio_effect; // no op
    in->stream.set_gain = in_set_gain;                              // no op
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;    // no op

    in->dev = adev;
    in->device = devices;
    in->stereo_to_mono_buf = NULL;
    in->stereo_to_mono_buf_size = 0;
    pthread_mutex_init(&in->lock, (const pthread_mutexattr_t *) NULL);

    memcpy(&in->req_config, config, sizeof(struct audio_config));

    memcpy(&in->pcm_config, &pcm_config_in, sizeof(struct pcm_config));
    in->pcm_config.rate = config->sample_rate;
    in->pcm_config.period_size = in->pcm_config.rate*IN_PERIOD_MS/1000;

    *stream_in = &in->stream;
error:
    return ret;
}


static int adev_dump(const audio_hw_device_t *dev, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *dev)
{
    struct generic_audio_device *adev = (struct generic_audio_device *)dev;
    int ret = 0;
    if (!adev)
        return 0;

    pthread_mutex_lock(&adev_init_lock);

    if (audio_device_ref_count == 0) {
        ALOGE("adev_close called when ref_count 0");
        ret = -EINVAL;
        goto error;
    }

    if ((--audio_device_ref_count) == 0) {
        free(adev);
    }

error:
    pthread_mutex_unlock(&adev_init_lock);
    return ret;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    static struct generic_audio_device *adev;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    pthread_once(&sFallbackOnce, fallback_init);
    if (sFallback != NULL) {
        return sFallback->common.methods->open(&sFallback->common, name, device);
    }

    pthread_mutex_lock(&adev_init_lock);
    if (audio_device_ref_count != 0) {
        *device = &adev->device.common;
        audio_device_ref_count++;
        ALOGV("%s: returning existing instance of adev", __func__);
        ALOGV("%s: exit", __func__);
        goto unlock;
    }
    adev = calloc(1, sizeof(struct generic_audio_device));

    pthread_mutex_init(&adev->lock, (const pthread_mutexattr_t *) NULL);

    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->device.common.module = (struct hw_module_t *) module;
    adev->device.common.close = adev_close;

    adev->device.init_check = adev_init_check;               // no op
    adev->device.set_voice_volume = adev_set_voice_volume;   // no op
    adev->device.set_master_volume = adev_set_master_volume; // no op
    adev->device.get_master_volume = adev_get_master_volume; // no op
    adev->device.set_master_mute = adev_set_master_mute;     // no op
    adev->device.get_master_mute = adev_get_master_mute;     // no op
    adev->device.set_mode = adev_set_mode;                   // no op
    adev->device.set_mic_mute = adev_set_mic_mute;
    adev->device.get_mic_mute = adev_get_mic_mute;
    adev->device.set_parameters = adev_set_parameters;       // no op
    adev->device.get_parameters = adev_get_parameters;       // no op
    adev->device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->device.open_output_stream = adev_open_output_stream;
    adev->device.close_output_stream = adev_close_output_stream;
    adev->device.open_input_stream = adev_open_input_stream;
    adev->device.close_input_stream = adev_close_input_stream;
    adev->device.dump = adev_dump;

    *device = &adev->device.common;

    audio_device_ref_count++;

unlock:
    pthread_mutex_unlock(&adev_init_lock);
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Generic audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};

/* This function detects whether or not we should be using an alsa audio device
 * or fall back to the legacy goldfish_audio driver.
 */
static void
fallback_init(void)
{
    void* module;

    FILE *fptr = fopen ("/proc/asound/pcm", "r");
    if (fptr != NULL) {
      // asound/pcm is empty if there are no devices
      int c = fgetc(fptr);
      fclose(fptr);
      if (c != EOF) {
          ALOGD("Emulator host-side ALSA audio emulation detected.");
          return;
      }
    }

    ALOGD("Emulator without host-side ALSA audio emulation detected.");
#if __LP64__
    module = dlopen("/system/lib64/hw/audio.primary.goldfish_legacy.so",
                    RTLD_LAZY|RTLD_LOCAL);
#else
    module = dlopen("/system/lib/hw/audio.primary.goldfish_legacy.so",
                    RTLD_LAZY|RTLD_LOCAL);
#endif
    if (module != NULL) {
        sFallback = (struct audio_module *)(dlsym(module, HAL_MODULE_INFO_SYM_AS_STR));
        if (sFallback == NULL) {
            dlclose(module);
        }
    }
    if (sFallback == NULL) {
        ALOGE("Could not find legacy fallback module!?");
    }
}
