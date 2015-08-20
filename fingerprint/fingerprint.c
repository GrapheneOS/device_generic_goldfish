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

/**
 * This is a very basic implementation of fingerprint to allow testing on the emulator. It
 * is *not* meant to be the final implementation on real devices.  For example,  it does *not*
 * implement all of the required features, such as secure template storage and recognition
 * inside a Trusted Execution Environment (TEE). However, this file is a reasonable starting
 * point as developers add fingerprint support to their platform.  See inline comments and
 * recommendations for details.
 *
 * Please see the Android Compatibility Definition Document (CDD) for a full list of requirements
 * and suggestions.
 */
#define  FINGERPRINT_LISTEN_SERVICE_NAME "fingerprintlisten"
#define  FINGERPRINT_TXT_FILENAME "/data/fingerprint.txt"

#define LOG_TAG "FingerprintHal"

// Typical devices will allow up to 5 fingerprints per user to maintain performance of 
// t < 500ms for recognition.  This is the total number of fingerprints we'll store.
#define MAX_NUM_FINGERS 32

#include <errno.h>
#include <endian.h>
#include <inttypes.h>
#include <malloc.h>
#include <string.h>
#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/fingerprint.h>
#include <hardware/qemud.h>

/**
 * Most devices will have an internal state machine resembling this. There are 3 basic states, as
 * shown below. When device is not authenticating or enrolling, it is expected to be in
 * the idle state.
 *
 * Note that this is completely independent of device wake state.  If the hardware device was in
 * the "scan" state when the device drops into power collapse, it should resume scanning when power
 * is restored.  This is to facilitate rapid touch-to-unlock from keyguard.
 */
typedef enum worker_state_t {
    STATE_ENROLL = 1,
    STATE_SCAN = 2,
    STATE_IDLE = 3,
} worker_state_t;

typedef struct worker_thread_t {
    pthread_t thread;
    pthread_mutex_t mutex;
    int request;
    worker_state_t state;
    int fingerid;
    int finger_is_on;
    int all_fingerids[MAX_NUM_FINGERS];
    uint64_t all_secureids[MAX_NUM_FINGERS];
    uint64_t all_authenids[MAX_NUM_FINGERS];
    int num_fingers_enrolled;
    FILE *fp_write;;
} worker_thread_t;

typedef struct emu_fingerprint_hal_device_t {
    fingerprint_device_t device; //inheritance
    worker_thread_t listener;
    uint64_t op_id;
    uint64_t challenge;
    uint64_t secure_user_id;
    uint64_t user_id;
    uint64_t authenticator_id;
    pthread_mutex_t lock;
} emu_fingerprint_hal_device_t;

static uint64_t get_64bit_rand() {
    // This should use a cryptographically-secure random number generator like arc4random().
    // It should be generated inside of the TEE where possible. Here we just use something
    // very simple.
    return (((uint64_t) rand()) << 32) | ((uint64_t) rand());
}

static void destroyListenerThread(emu_fingerprint_hal_device_t* dev)
{
    pthread_join(dev->listener.thread, NULL);
    pthread_mutex_destroy(&dev->listener.mutex);
}

bool finger_already_enrolled(emu_fingerprint_hal_device_t* dev) {
    int i;
    for (i = 0; i < dev->listener.num_fingers_enrolled; ++ i) {
        if (dev->listener.fingerid == dev->listener.all_fingerids[i % MAX_NUM_FINGERS]) {
            dev->secure_user_id = dev->listener.all_secureids[i % MAX_NUM_FINGERS];
            dev->authenticator_id = dev->listener.all_authenids[i % MAX_NUM_FINGERS];
            return true;
        }
    }
    return false;
}

static void save_fingerid(FILE* fp, int fingerid, uint64_t secureid, uint64_t authenid) {
    if (!fp) return;
    fprintf(fp, " %d %" PRIu64 " %" PRIu64, fingerid, secureid, authenid);
    fflush(fp);
}

/**
 * This is the communication channel from the HAL layer to fingerprintd.
 */
static void listener_send_notice(emu_fingerprint_hal_device_t* dev)
{
    fingerprint_msg_t message = {0};
    bool is_authentication = false;
    bool is_valid_finger = false;
    pthread_mutex_lock(&dev->listener.mutex);
    if (dev->listener.state == STATE_ENROLL) {
        message.type = FINGERPRINT_TEMPLATE_ENROLLING;
        message.data.enroll.finger.fid = dev->listener.fingerid;
        message.data.enroll.samples_remaining = 0;
        dev->authenticator_id = get_64bit_rand();
        dev->listener.state = STATE_SCAN;
        if (!finger_already_enrolled(dev)) {
            const int n = dev->listener.num_fingers_enrolled % MAX_NUM_FINGERS;
            dev->listener.all_fingerids[n] = dev->listener.fingerid;
            dev->listener.all_secureids[n] = dev->secure_user_id;
            dev->listener.all_authenids[n] = dev->authenticator_id;
            ++ dev->listener.num_fingers_enrolled;
            save_fingerid(dev->listener.fp_write, dev->listener.fingerid, dev->secure_user_id,
                          dev->authenticator_id);
            is_valid_finger = true;
        }
    } else {
        is_authentication = true;
        is_valid_finger = finger_already_enrolled(dev);
        message.type = FINGERPRINT_AUTHENTICATED;
        message.data.authenticated.finger.gid = 0;
        message.data.authenticated.finger.fid = is_valid_finger ? dev->listener.fingerid : 0;
        message.data.authenticated.hat.version = HW_AUTH_TOKEN_VERSION;
        message.data.authenticated.hat.authenticator_type = htobe32(HW_AUTH_FINGERPRINT);
        message.data.authenticated.hat.challenge = dev->op_id;
        message.data.authenticated.hat.authenticator_id = dev->authenticator_id;
        message.data.authenticated.hat.user_id = dev->secure_user_id;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        message.data.authenticated.hat.timestamp =
            htobe64((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }
    pthread_mutex_unlock(&dev->listener.mutex);

    pthread_mutex_lock(&dev->lock);
    if (is_authentication) {
        fingerprint_msg_t acquired_message = {0};
        acquired_message.type = FINGERPRINT_ACQUIRED;
        message.data.acquired.acquired_info = FINGERPRINT_ACQUIRED_GOOD;
        dev->device.notify(&acquired_message);
    }
    if (is_valid_finger || is_authentication) {
        dev->device.notify(&message);
    }
    pthread_mutex_unlock(&dev->lock);
}

/**
 * This a very simple event loop for the fingerprint sensor. For a given state (enroll, scan),
 * this would receive events from the sensor and forward them to fingerprintd using the
 * notify() method.
 *
 * In this simple example, we open a qemu channel (a pipe) where the developer can inject events to
 * exercise the API and test application code.
 *
 * The scanner should remain in the scanning state until either an error occurs or the operation
 * completes.
 *
 * Recoverable errors such as EINTR should be handled locally;  they should not
 * be propagated unless there's something the user can do about it (e.g. "clean sensor"). Such
 * messages should go through the onAcquired() interface.
 *
 * If an unrecoverable error occurs, an acquired message (e.g. ACQUIRED_PARTIAL) should be sent,
 * followed by an error message (e.g. FINGERPRINT_ERROR_UNABLE_TO_PROCESS).
 *
 * Note that this event loop would typically run in TEE since it must interact with the sensor
 * hardware and handle raw fingerprint data and encrypted templates.  It is expected that
 * this code monitors the TEE for resulting events, such as enrollment and authentication status.
 * Here we just have a very simple event loop that monitors a qemu channel for pseudo events.
 */
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

    int i;
    for (i = 0; i < MAX_NUM_FINGERS; ++ i) {
        dev->listener.all_fingerids[i] = 0;
    }
    //read registered fingerprint ids from /data/local/fingerprint.txt
    //TODO: store it in a better location
    dev->listener.num_fingers_enrolled = 0;
    FILE* fp_stored = fopen(FINGERPRINT_TXT_FILENAME, "r");
    if (fp_stored) {
        while (1) {
            int fingerid = 0;
            uint64_t secureid = 0;
            uint64_t authenid = 0;
            if(fscanf(fp_stored, "%d %" SCNu64 " %" SCNu64, &fingerid, &secureid, &authenid) == 3) {
                const int n = dev->listener.num_fingers_enrolled % MAX_NUM_FINGERS;
                dev->listener.all_fingerids[n] = fingerid;
                dev->listener.all_secureids[n] = secureid;
                dev->listener.all_authenids[n] = authenid;
                ++ dev->listener.num_fingers_enrolled;
            } else {
                break;
            }
        }
        fclose(fp_stored);
    }

    dev->listener.fp_write = fopen(FINGERPRINT_TXT_FILENAME, "a");

    char buffer[128];
    int fingerid=-1;
    int size;
    while (1) {
        //simply listen in blocking mode
        if ((size = qemud_channel_recv(fd, buffer, sizeof buffer - 1)) >0) {
            buffer[size] = '\0';
            if (sscanf(buffer, "on:%d", &fingerid) == 1) {
                if (fingerid > 0 ) {
                    dev->listener.fingerid = fingerid;
                    dev->listener.finger_is_on = 1;
                    ALOGD("got finger %d", fingerid);
                    listener_send_notice(dev);
                    ALOGD("send notice finger %d", fingerid);
                }
                else {
                    ALOGE("finger id should be positive");
                }
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

static uint64_t fingerprint_get_auth_id(struct fingerprint_device __unused *device) {
    // This should return the authentication_id generated when the fingerprint template database
    // was created.  Though this isn't expected to be secret, it is reasonable to expect it to be
    // cryptographically generated to avoid replay attacks.
    emu_fingerprint_hal_device_t* dev = (emu_fingerprint_hal_device_t*) device;
    return dev->authenticator_id;
}

static int fingerprint_set_active_group(struct fingerprint_device __unused *device, uint32_t gid,
        const char *path) {
    // Groups are a future feature.  For now, the framework sends the profile owner's id (userid)
    // as the primary group id for the user.  This code should create a tuple (groupId, fingerId)
    // that represents a single fingerprint entity in the database.  For now we just generate
    // globally unique ids.
    return 0;
}

/**
 * If fingerprints are enrolled, then this function is expected to put the sensor into a
 * "scanning" state where it's actively scanning and recognizing fingerprint features.
 * Actual authentication must happen in TEE and should be monitored in a separate thread
 * since this function is expected to return immediately.
 */
static int fingerprint_authenticate(struct fingerprint_device __unused *device,
    uint64_t __unused operation_id, __unused uint32_t gid)
{
    ALOGD("fingerprint_authenticate");

    emu_fingerprint_hal_device_t* dev = (emu_fingerprint_hal_device_t*) device;
    pthread_mutex_lock(&dev->lock);
    dev->op_id = operation_id;
    pthread_mutex_unlock(&dev->lock);
    setListenerState(dev, STATE_SCAN);
    return 0;
}

/**
 * This is expected to put the sensor into an "enroll" state where it's actively scanning and
 * working towards a finished fingerprint database entry. Authentication must happen in
 * a separate thread since this function is expected to return immediately.
 *
 * Note: This method should always generate a new random authenticator_id.
 *
 * Note: As with fingerprint_authenticate(), this would run in TEE on a real device.
 */
static int fingerprint_enroll(struct fingerprint_device *device,
        const hw_auth_token_t *hat,
        uint32_t __unused gid,
        uint32_t __unused timeout_sec) {
    ALOGD("fingerprint_enroll");
    emu_fingerprint_hal_device_t* dev = (emu_fingerprint_hal_device_t*) device;
    if (hat && hat->challenge == dev->challenge) {
        // The secure_user_id retrieved from the auth token should be stored
        // with the enrolled fingerprint template and returned in the auth result
        // for a successful authentication with that finger.
        dev->secure_user_id = hat->user_id;
    } else {
        ALOGW("%s: invalid or null auth token", __func__);
    }

    if (hat->version != HW_AUTH_TOKEN_VERSION) {
        return -EPROTONOSUPPORT;
    }
    if (hat->challenge != dev->challenge && !(hat->authenticator_type & HW_AUTH_FINGERPRINT)) {
        return -EPERM;
    }

    dev->user_id = hat->user_id;

    // TODO: store enrolled fingerprints, authenticator id, and secure_user_id
    setListenerState(dev, STATE_ENROLL);
    return 0;

}

/**
 * The pre-enrollment step is simply to get an authentication token that can be wrapped and
 * verified at a later step.  The primary purpose is to return a token that protects against
 * spoofing and replay attacks. It is passed to password authentication where it is wrapped and
 * propagated to the enroll step.
 */
static uint64_t fingerprint_pre_enroll(struct fingerprint_device *device) {
    ALOGD("fingerprint_pre_enroll");
    emu_fingerprint_hal_device_t* dev = (emu_fingerprint_hal_device_t*) device;
    // The challenge will typically be a cryptographically-secure key
    // coming from the TEE so it can be verified at a later step. For now we just generate a
    // random value.
    dev->challenge = get_64bit_rand();
    return dev->challenge;
}

/**
 * Cancel is called by the framework to cancel an outstanding event.  This should *not* be called
 * by the driver since it will cause the framework to stop listening for fingerprints.
 */
static int fingerprint_cancel(struct fingerprint_device __unused *device) {
    ALOGD("fingerprint_cancel");
    emu_fingerprint_hal_device_t* dev = (emu_fingerprint_hal_device_t*) device;
    setListenerState(dev, STATE_IDLE);
    return 0;
}

static int fingerprint_enumerate(struct fingerprint_device *device,
        fingerprint_finger_id_t *results, uint32_t *max_size) {
    // TODO: implement me
    return 0;
}

static int fingerprint_remove(struct fingerprint_device __unused *dev,
        uint32_t __unused gid, uint32_t __unused fid) {
    // TODO: implement enroll and remove, and set dev->authenticator_id = 0 when no FPs enrolled
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
        ALOGD("fingerprint open\n");
    }

    emu_fingerprint_hal_device_t *dev = malloc(sizeof(emu_fingerprint_hal_device_t));
    memset(dev, 0, sizeof(emu_fingerprint_hal_device_t));

    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = HARDWARE_MODULE_API_VERSION(2, 0);
    dev->device.common.module = (struct hw_module_t*) module;
    dev->device.common.close = fingerprint_close;
    dev->device.pre_enroll = fingerprint_pre_enroll;
    dev->device.enroll = fingerprint_enroll;
    dev->device.get_authenticator_id = fingerprint_get_auth_id;
    dev->device.set_active_group = fingerprint_set_active_group;
    dev->device.authenticate = fingerprint_authenticate;
    dev->device.cancel = fingerprint_cancel;
    dev->device.enumerate = fingerprint_enumerate;
    dev->device.remove = fingerprint_remove;
    dev->device.set_notify = set_notify_callback;
    dev->device.notify = NULL;

    // This is typically a cryptographically-secure token generated when the private fingerprint
    // template database is created.  For simplicity of this driver, we store a recognizable value.
    //
    // Real devices should *not* use this token!
    dev->authenticator_id = 0xdeadbeef;

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
        .module_api_version = FINGERPRINT_MODULE_API_VERSION_2_0,
        .hal_api_version    = HARDWARE_HAL_API_VERSION,
        .id                 = FINGERPRINT_HARDWARE_MODULE_ID,
        .name               = "Emulator Fingerprint HAL",
        .author             = "The Android Open Source Project",
        .methods            = &fingerprint_module_methods,
    },
};
