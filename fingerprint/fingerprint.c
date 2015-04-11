/*
 * Copyright (C) 2015 The Android Open Source Project
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

#define  FINGERPRINT_LISTEN_SERVICE_NAME "fingerprintlisten"

#define LOG_TAG "FingerprintHal"

#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/fingerprint.h>
#include <hardware/qemud.h>

typedef enum worker_state_t {
    STATE_ENROLL = 1,
    STATE_SCAN = 2,
    STATE_IDLE = 3,
    STATE_EXIT = 4
} worker_state_t;

typedef struct worker_thread_t {
    pthread_t thread;
    pthread_mutex_t mutex;
    int request;
    worker_state_t state;
    int fingerid;
    int finger_is_on;
} worker_thread_t;

typedef struct emu_fingerprint_hal_device_t {
    fingerprint_device_t device; //inheritance
    worker_thread_t listener;
    pthread_mutex_t lock;
} emu_fingerprint_hal_device_t;


static void destroyListenerThread(emu_fingerprint_hal_device_t* dev)
{
    pthread_join(dev->listener.thread, NULL);
    pthread_mutex_destroy(&dev->listener.mutex);
}

static void listener_send_notice(emu_fingerprint_hal_device_t* dev)
{
    fingerprint_msg_t message;
    pthread_mutex_lock(&dev->listener.mutex);
    if (dev->listener.state == STATE_ENROLL) {
        message.type = FINGERPRINT_TEMPLATE_ENROLLING;
        message.data.enroll.finger.fid = dev->listener.fingerid;
        message.data.enroll.samples_remaining = 0;
        dev->listener.state = STATE_SCAN;
    } else {
        message.type = FINGERPRINT_PROCESSED;
        message.data.processed.finger.gid = 0;
        message.data.processed.finger.fid = dev->listener.fingerid;
    }
    pthread_mutex_unlock(&dev->listener.mutex);

    pthread_mutex_lock(&dev->lock);
    dev->device.notify(message);
    pthread_mutex_unlock(&dev->lock);
}

static void* listenerFunction(void* data)
{
    emu_fingerprint_hal_device_t* dev = (emu_fingerprint_hal_device_t*) data;

    int fd = qemud_channel_open(FINGERPRINT_LISTEN_SERVICE_NAME);
    if (fd < 0) {
        ALOGE("listener cannot open fingerprint listener service exit");
        return NULL;
    }

    const char* cmd = "listen";
    if (qemud_channel_send(fd, cmd, strlen(cmd)) < 0) {
        ALOGE("cannot write fingerprint 'listen' to host");
        return NULL;
    }

    char buffer[128];
    int fingerid=-1;
    int size;
    while (1) {
        //simply listen in blocking mode
        if ((size = qemud_channel_recv(fd, buffer, sizeof buffer - 1)) >0) {
            buffer[size] = '\0';
            if (sscanf(buffer, "on:%d", &fingerid) == 1) {
                dev->listener.fingerid = fingerid;
                dev->listener.finger_is_on = 1;
                ALOGD("got finger %d", fingerid);
                listener_send_notice(dev);
                ALOGD("send notice finger %d", fingerid);
            } else if (strncmp("off", buffer, 3) == 0) {
                dev->listener.finger_is_on = 0;
                ALOGD("finger off %d", fingerid);
            } else {
                ALOGE("error: '%s'", buffer);
            }
        } else {
            ALOGE("receive failure");
            // return NULL;
        }
        //TODO: check for request to exit thread
    }

    ALOGD("listener exit");
    return NULL;
}

static void createListenerThread(emu_fingerprint_hal_device_t* dev)
{
    pthread_mutex_init(&dev->listener.mutex, NULL);
    pthread_create(&dev->listener.thread, NULL, listenerFunction, dev);
}

static int fingerprint_close(hw_device_t *dev)
{
    if (dev) {
        destroyListenerThread((emu_fingerprint_hal_device_t*) dev);
        free(dev);
        return 0;
    } else {
        return -1;
    }
}

static void setListenerState(emu_fingerprint_hal_device_t* dev, worker_state_t state) {
    pthread_mutex_lock(&dev->listener.mutex);
    dev->listener.state = state;
    pthread_mutex_unlock(&dev->listener.mutex);
}

static int fingerprint_authenticate(struct fingerprint_device __unused *device,
    uint64_t __unused operation_id, __unused uint32_t gid)
{
    ALOGE("fingerprint_authenticate");

    emu_fingerprint_hal_device_t* dev = (emu_fingerprint_hal_device_t*) device;
    setListenerState(dev, STATE_SCAN);
    return 0;
}

static int fingerprint_enroll(struct fingerprint_device __unused *device,
        uint32_t __unused gid,
        uint32_t __unused timeout_sec) {
    ALOGE("fingerpring_enroll");
    emu_fingerprint_hal_device_t* dev = (emu_fingerprint_hal_device_t*) device;
    setListenerState(dev, STATE_ENROLL);
    return 0;

}

static int fingerprint_cancel(struct fingerprint_device __unused *device) {
    ALOGE("fingerpring_cancel");
    emu_fingerprint_hal_device_t* dev = (emu_fingerprint_hal_device_t*) device;
    setListenerState(dev, STATE_IDLE);
    return 0;
}

static int fingerprint_remove(struct fingerprint_device __unused *dev,
        fingerprint_finger_id_t __unused fingerprint_id) {
    return FINGERPRINT_ERROR;
}

static int set_notify_callback(struct fingerprint_device *device,
                                fingerprint_notify_t notify) {
    ALOGD("set_notify");
    emu_fingerprint_hal_device_t* dev =(emu_fingerprint_hal_device_t*) device;
    pthread_mutex_lock(&dev->lock);
    device->notify = notify;
    pthread_mutex_unlock(&dev->lock);
    return 0;
}

static int fingerprint_open(const hw_module_t* module, const char __unused *id,
                            hw_device_t** device)
{
    if (device == NULL) {
        ALOGE("NULL device on open");
        return -EINVAL;
    } else {
        ALOGE("fingerprint open\n");
    }

    emu_fingerprint_hal_device_t *dev = malloc(sizeof(emu_fingerprint_hal_device_t));
    memset(dev, 0, sizeof(emu_fingerprint_hal_device_t));

    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = HARDWARE_MODULE_API_VERSION(2, 0);
    dev->device.common.module = (struct hw_module_t*) module;
    dev->device.common.close = fingerprint_close;

    dev->device.enroll = fingerprint_enroll;
    dev->device.cancel = fingerprint_cancel;
    dev->device.authenticate = fingerprint_authenticate;
    dev->device.remove = fingerprint_remove;
    dev->device.set_notify = set_notify_callback;
    dev->device.notify = NULL;

    pthread_mutex_init(&dev->lock, NULL);
    createListenerThread(dev);
    *device = (hw_device_t*) dev;
    return 0;
}

static struct hw_module_methods_t fingerprint_module_methods = {
    .open = fingerprint_open,
};

fingerprint_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag                = HARDWARE_MODULE_TAG,
        .module_api_version = FINGERPRINT_MODULE_API_VERSION_1_0,
        .hal_api_version    = HARDWARE_HAL_API_VERSION,
        .id                 = FINGERPRINT_HARDWARE_MODULE_ID,
        .name               = "Emulator Fingerprint HAL",
        .author             = "The Android Open Source Project",
        .methods            = &fingerprint_module_methods,
    },
};
