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
#include <assert.h>
#include <errno.h>
#include <hardware/keymaster_common.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define LOG_TAG "OpenSSLKeyMaster"
#include <cutils/log.h>

#ifdef DEBUG
#   define D(...)   ALOGD(__VA_ARGS__)
#else
#   define D(...)   ((void)0)
#endif

#include <hardware/hardware.h>
#include <hardware/keymaster0.h>
#include "qemud.h"

#define KEYMASTER_SERVICE_NAME "KeymasterService"

typedef struct qemu_keymaster0_device_t {
    keymaster0_device_t device; // "inheritance", must be the first member
    int qchanfd;
    pthread_mutex_t lock;
} qemu_keymaster0_device_t;

enum {
        GenerateKeypair = 0,
        ImportKeypair,
        GetKeypairPublic,
        SignData,
        VerifyData,
};

static int32_t unwrap_key_type(const uint8_t* key_blob) {
    int32_t key_type = 0;
    const uint8_t* p = key_blob;
    size_t i;
    for (i = 0; i < sizeof(key_type); i++) {
        key_type = (key_type << 8) | *p++;
    }
    return key_type;
}

static int keymaster_fd_write(int fd, const void* buff, int len) {
    int ret = WriteFully(fd, buff, len);
    assert(ret);
    return ret;
}

static int keymaster_fd_read(int fd, void* buff, int len) {
    int ret = ReadFully(fd, buff, len);
    assert(ret);
    return ret;
}

static int EVP2EncoderType(int evpType) {
    switch (evpType) {
        case 6: // RSA
            return TYPE_RSA;
        case 408: // EC:
            return TYPE_EC;
        case 116: // DSA:
            return TYPE_DSA;
        default:
            D("Unsupported evp key type %d", evpType);
            return -1;
    }
}

static int gen_param_length(int type, const void* params) {
    switch (type) {
        case TYPE_DSA: {
            keymaster_dsa_keygen_params_t* dsa_params =
                    (keymaster_dsa_keygen_params_t*)params;
            return 4 + 4 + 4 + 4 + dsa_params->generator_len
                    + dsa_params->prime_p_len
                    + dsa_params->prime_q_len;
        }
        case TYPE_EC:
            return 4;
        case TYPE_RSA:
            return 8 + 4;
        default:
            D("Unsupported key type %d", type);
            return -1;
    }
}

static int sign_param_length(int type) {
    switch (type) {
        case TYPE_DSA:
            return sizeof(keymaster_digest_algorithm_t);
            break;
        case TYPE_EC:
            return sizeof(keymaster_digest_algorithm_t);
            break;
        case TYPE_RSA:
            return sizeof(keymaster_digest_algorithm_t)
                    + sizeof(keymaster_rsa_padding_t);
            break;
        default:
            D("Unsupported key type %d", type);
            return -1;
    }
}

static void write_gen_param(int fd,
    const keymaster_keypair_t type,
    const void* key_params) {
    switch (type) {
        case TYPE_DSA: {
            keymaster_dsa_keygen_params_t* dsa_params =
                (keymaster_dsa_keygen_params_t*)key_params;

            keymaster_fd_write(fd, (void*)&dsa_params->key_size,
                sizeof(dsa_params->key_size));

            keymaster_fd_write(fd, (void*)&dsa_params->generator_len,
                sizeof(dsa_params->generator_len));
            keymaster_fd_write(fd, (void*)&dsa_params->prime_p_len,
                sizeof(dsa_params->prime_p_len));
            keymaster_fd_write(fd, (void*)&dsa_params->prime_q_len,
                sizeof(dsa_params->prime_q_len));

            keymaster_fd_write(fd, (void*)dsa_params->generator,
                dsa_params->generator_len);
            keymaster_fd_write(fd, (void*)dsa_params->prime_p,
                dsa_params->prime_p_len);
            keymaster_fd_write(fd, (void*)dsa_params->prime_q,
                dsa_params->prime_q_len);
            break;
        }
        case TYPE_EC: {
            keymaster_ec_keygen_params_t* ec_params =
                (keymaster_ec_keygen_params_t*)key_params;
            keymaster_fd_write(fd, (void*)&ec_params->field_size,
                sizeof(ec_params->field_size));
            break;
        }
        case TYPE_RSA: {
            keymaster_rsa_keygen_params_t* rsa_params =
                (keymaster_rsa_keygen_params_t*)key_params;
            keymaster_fd_write(fd, (void*)&rsa_params->modulus_size,
                sizeof(rsa_params->modulus_size));
            keymaster_fd_write(fd, (void*)&rsa_params->public_exponent,
                sizeof(rsa_params->public_exponent));
            break;
        }
        default:
            D("Unsupported key type %d", type);
            return;
    }
}

static void write_sign_param(int fd,
    const keymaster_keypair_t type,
    const void* key_params) {
    switch (type) {
        case TYPE_DSA: {
            keymaster_dsa_sign_params_t* dsa_params =
                (keymaster_dsa_sign_params_t*)key_params;
            keymaster_fd_write(fd, (void*)&dsa_params->digest_type,
                sizeof(dsa_params->digest_type));
            break;
        }
        case TYPE_EC: {
            keymaster_ec_sign_params_t* ec_params =
                (keymaster_ec_sign_params_t*)key_params;
            keymaster_fd_write(fd, (void*)&ec_params->digest_type,
                sizeof(ec_params->digest_type));
            break;
        }
        case TYPE_RSA: {
            keymaster_rsa_sign_params_t* rsa_params =
                (keymaster_rsa_sign_params_t*)key_params;
            keymaster_fd_write(fd, (void*)&rsa_params->digest_type,
                sizeof(rsa_params->digest_type));
            keymaster_fd_write(fd, (void*)&rsa_params->padding_type,
                sizeof(rsa_params->padding_type));
            break;
        }
        default:
            D("Unsupported key type %d", type);
            return;
    }
}

static int openssl_generate_keypair(const keymaster0_device_t* device,
        const keymaster_keypair_t key_type, const void* key_params,
        uint8_t** key_blob, size_t* key_blob_length) {
    D("generate keypair");
    if (key_params == NULL) {
        ALOGE("key_params == null");
        return -1;
    }
    const uint32_t cmd = GenerateKeypair;
    int32_t _key_type = (int)key_type;
    int32_t key_params_len = gen_param_length(key_type, key_params);
    if (key_params_len < 0) {
        return -1;
    }
    qemu_keymaster0_device_t* qemu_dev = (qemu_keymaster0_device_t*)device;
    pthread_mutex_lock(&qemu_dev->lock);
    // send
    const uint64_t cmdLen = sizeof(cmd) + sizeof(_key_type) + sizeof(key_params_len)
            + key_params_len;
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&cmdLen, sizeof(cmdLen));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&cmd, sizeof(cmd));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&_key_type, sizeof(_key_type));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&key_params_len, sizeof(key_params_len));
    write_gen_param(qemu_dev->qchanfd, key_type, key_params);

    // receive
    uint32_t _key_blob_len = 0;
    int32_t ret;
    keymaster_fd_read(qemu_dev->qchanfd, &_key_blob_len, sizeof(_key_blob_len));
    *key_blob_length = _key_blob_len;
    if (_key_blob_len) {
        *key_blob = malloc(_key_blob_len);
        keymaster_fd_read(qemu_dev->qchanfd, *key_blob, _key_blob_len);
    }
    keymaster_fd_read(qemu_dev->qchanfd, &ret, sizeof(ret));

    pthread_mutex_unlock(&qemu_dev->lock);
    return ret;
}

static int openssl_import_keypair(const keymaster0_device_t* device,
                                                                  const uint8_t* key,
                                                                  const size_t key_length,
                                                                  uint8_t** key_blob,
                                                                  size_t* key_blob_length) {
    D("import keypair");
    if (key == NULL) {
        ALOGW("input key == NULL");
        return -1;
    } else if (key_blob == NULL || key_blob_length == NULL) {
        ALOGW("output key blob or length == NULL");
        return -1;
    }

    const uint32_t cmd = ImportKeypair;
    qemu_keymaster0_device_t* qemu_dev = (qemu_keymaster0_device_t*)device;
    pthread_mutex_lock(&qemu_dev->lock);
    // send
    const uint32_t _key_length = (uint32_t)key_length;
    const uint64_t cmdLen = sizeof(cmd) + sizeof(_key_length) + _key_length;

    keymaster_fd_write(qemu_dev->qchanfd, (void*)&cmdLen, sizeof(cmdLen));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&cmd, sizeof(cmd));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&_key_length, sizeof(_key_length));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)key, key_length);

    // receive
    uint32_t _key_blob_len = 0;
    int32_t ret;
    keymaster_fd_read(qemu_dev->qchanfd, &_key_blob_len, sizeof(_key_blob_len));
    *key_blob_length = _key_blob_len;
    *key_blob = malloc(_key_blob_len);
    keymaster_fd_read(qemu_dev->qchanfd, *key_blob, _key_blob_len);
    keymaster_fd_read(qemu_dev->qchanfd, &ret, sizeof(ret));

    pthread_mutex_unlock(&qemu_dev->lock);
    return ret;
}

static int openssl_get_keypair_public(const keymaster0_device_t* device,
        const uint8_t* key_blob, const size_t key_blob_length,
        uint8_t** x509_data, size_t* x509_data_length) {
    D("get keypair public");
    if (x509_data == NULL || x509_data_length == NULL) {
        ALOGW("output public key buffer == NULL");
        return -1;
    }

    const uint32_t cmd = GetKeypairPublic;
    qemu_keymaster0_device_t* qemu_dev = (qemu_keymaster0_device_t*)device;
    pthread_mutex_lock(&qemu_dev->lock);
    // send
    const uint32_t _key_blob_length = (uint32_t)key_blob_length;
    const uint64_t cmdLen = sizeof(cmd) + sizeof(_key_blob_length) +
                            _key_blob_length;

    keymaster_fd_write(qemu_dev->qchanfd, (void*)&cmdLen, sizeof(cmdLen));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&cmd, sizeof(cmd));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&_key_blob_length, sizeof(_key_blob_length));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)key_blob, _key_blob_length);

    // receive
    uint32_t _x509_data_length = 0;
    int32_t ret;
    keymaster_fd_read(qemu_dev->qchanfd, &_x509_data_length,
            sizeof(_x509_data_length));
    *x509_data_length = _x509_data_length;
    if (_x509_data_length) {
        *x509_data = malloc(_x509_data_length);
        keymaster_fd_read(qemu_dev->qchanfd, *x509_data, _x509_data_length);
    } else {
        *x509_data = NULL;
    }
    keymaster_fd_read(qemu_dev->qchanfd, &ret, sizeof(ret));

    pthread_mutex_unlock(&qemu_dev->lock);
    return ret;
}

static int openssl_sign_data(const keymaster0_device_t* device, const void* params,
        const uint8_t* key_blob,  const size_t key_blob_length,
        const uint8_t* data, const size_t data_length,
        uint8_t** signed_data, size_t* signed_data_length) {
    D("sign data");
    if (signed_data == NULL || signed_data_length == NULL) {
        ALOGW("output signature buffer == NULL");
        return -1;
    }

    int32_t key_type = EVP2EncoderType(unwrap_key_type(key_blob));
    int32_t params_len = sign_param_length(key_type);
    if (params_len < 0) {
        return -1;
    }

    const uint32_t cmd = SignData;
    qemu_keymaster0_device_t* qemu_dev = (qemu_keymaster0_device_t*)device;
    pthread_mutex_lock(&qemu_dev->lock);
    // send
    const uint32_t _key_blob_length = (uint32_t)key_blob_length;
    const uint32_t _data_length = (uint32_t)data_length;
    const uint64_t cmdLen = sizeof(cmd) + sizeof(key_type) + sizeof(params_len) + params_len +
                            sizeof(_key_blob_length) + _key_blob_length +
                            sizeof(_data_length) + _data_length;

    keymaster_fd_write(qemu_dev->qchanfd, (void*)&cmdLen, sizeof(cmdLen));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&cmd, sizeof(cmd));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&key_type, sizeof(key_type));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&params_len, sizeof(params_len));
    write_sign_param(qemu_dev->qchanfd, key_type, params);
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&_key_blob_length, sizeof(_key_blob_length));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)key_blob, _key_blob_length);
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&_data_length, sizeof(_data_length));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)data, _data_length);

    // receive
    uint32_t _signed_data_length = 0;
    int32_t ret;
    keymaster_fd_read(qemu_dev->qchanfd, &_signed_data_length,
            sizeof(_signed_data_length));
    *signed_data_length = _signed_data_length;
    if (_signed_data_length) {
        *signed_data = malloc(_signed_data_length);
        keymaster_fd_read(qemu_dev->qchanfd, *signed_data, _signed_data_length);
    }
    keymaster_fd_read(qemu_dev->qchanfd, &ret, sizeof(ret));

    pthread_mutex_unlock(&qemu_dev->lock);
    return ret;
}

static int openssl_verify_data(const keymaster0_device_t* device, const void* params,
        const uint8_t* key_blob, const size_t key_blob_length,
        const uint8_t* signed_data, const size_t signed_data_length,
        const uint8_t* signature, const size_t signature_length) {
    D("verify data");
    if (signed_data == NULL || signature == NULL) {
        ALOGW("data or signature buffers == NULL");
        return -1;
    }

    int32_t key_type = EVP2EncoderType(unwrap_key_type(key_blob));
    int32_t params_len = sign_param_length(key_type);
    if (params_len < 0) {
        return -1;
    }

    const uint32_t cmd = VerifyData;
    qemu_keymaster0_device_t* qemu_dev = (qemu_keymaster0_device_t*)device;
    pthread_mutex_lock(&qemu_dev->lock);
    // send
    const uint32_t _key_blob_length = (uint32_t)key_blob_length;
    const uint32_t _signed_data_length = (uint32_t)signed_data_length;
    const uint32_t _signature_length = (uint32_t)signature_length;
    const uint64_t cmdLen = sizeof(cmd) + sizeof(key_type) + sizeof(params_len) + params_len +
                            sizeof(_key_blob_length) + _key_blob_length +
                            sizeof(_signed_data_length) + _signed_data_length +
                            sizeof(_signature_length) + _signature_length;

    keymaster_fd_write(qemu_dev->qchanfd, (void*)&cmdLen, sizeof(cmdLen));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&cmd, sizeof(cmd));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&key_type, sizeof(key_type));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&params_len, sizeof(params_len));
    write_sign_param(qemu_dev->qchanfd, key_type, params);
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&_key_blob_length, sizeof(_key_blob_length));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)key_blob, _key_blob_length);
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&_signed_data_length, sizeof(_signed_data_length));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)signed_data, _signed_data_length);
    keymaster_fd_write(qemu_dev->qchanfd, (void*)&_signature_length, sizeof(_signature_length));
    keymaster_fd_write(qemu_dev->qchanfd, (void*)signature, _signature_length);

    // receive
    int32_t ret;
    keymaster_fd_read(qemu_dev->qchanfd, &ret, sizeof(ret));

    pthread_mutex_unlock(&qemu_dev->lock);
    return ret;
}

/* Close an opened OpenSSL instance */
static int openssl_close(hw_device_t* dev) {
    D("close device");
    qemu_keymaster0_device_t* qemu_dev = (qemu_keymaster0_device_t*)dev;
    pthread_mutex_destroy(&qemu_dev->lock);
    close(qemu_dev->qchanfd);
    free(qemu_dev);
    return 0;
}

/*
 * Generic device handling
 */
static int openssl_open(const hw_module_t* module, const char* name,
                        hw_device_t** device) {
    D("open device");
    if (strcmp(name, KEYSTORE_KEYMASTER) != 0)
        return -EINVAL;

    qemu_keymaster0_device_t* qemu_dev = calloc(1, sizeof(qemu_keymaster0_device_t));
    if (qemu_dev == NULL)
        return -ENOMEM;
    keymaster0_device_t* dev = (keymaster0_device_t*)qemu_dev;

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 1;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = openssl_close;

    dev->flags = KEYMASTER_BLOBS_ARE_STANDALONE | KEYMASTER_SUPPORTS_DSA |
                 KEYMASTER_SUPPORTS_EC;

    dev->generate_keypair = openssl_generate_keypair;
    dev->import_keypair = openssl_import_keypair;
    dev->get_keypair_public = openssl_get_keypair_public;
    dev->delete_keypair = NULL;
    dev->delete_all = NULL;
    dev->sign_data = openssl_sign_data;
    dev->verify_data = openssl_verify_data;

    // set up the pipe
    qemu_dev->qchanfd = qemu_pipe_open(KEYMASTER_SERVICE_NAME);
    if (qemu_dev->qchanfd < 0) {
        ALOGE("keymaster: failed to get host connection while opening %s\n", name);
        free(qemu_dev);
        return -EIO;
    }
    pthread_mutex_init(&qemu_dev->lock, NULL);

    *device = &dev->common;

    return 0;
}

static struct hw_module_methods_t keystore_module_methods = {
    .open = openssl_open,
};

struct keystore_module goldfishkeymaster_module __attribute__((visibility("default"))) = {
    .common =
        {
         .tag = HARDWARE_MODULE_TAG,
         .module_api_version = KEYMASTER_MODULE_API_VERSION_0_2,
         .hal_api_version = HARDWARE_HAL_API_VERSION,
         .id = KEYSTORE_HARDWARE_MODULE_ID,
         .name = "Keymaster OpenSSL HAL",
         .author = "The Android Open Source Project",
         .methods = &keystore_module_methods,
         .dso = 0,
         .reserved = {},
        },
};
