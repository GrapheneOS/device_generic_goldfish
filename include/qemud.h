/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef ANDROID_INCLUDE_HARDWARE_QEMUD_H
#define ANDROID_INCLUDE_HARDWARE_QEMUD_H

#include "qemu_pipe.h"

static __inline__ QEMU_PIPE_HANDLE
qemud_channel_open(const char*  name)
{
    return qemu_pipe_open_ns("qemud", name, O_RDWR);
}

static __inline__ int
qemud_channel_send(QEMU_PIPE_HANDLE pipe, const void*  msg, int  msglen)
{
    char  header[5];

    if (msglen < 0)
        msglen = strlen((const char*)msg);

    if (msglen == 0)
        return 0;

    snprintf(header, sizeof(header), "%04x", msglen);
    if (qemu_pipe_write_fully(pipe, header, 4)) {
        D("can't write qemud frame header: %s", strerror(errno));
        return -1;
    }

    if (qemu_pipe_write_fully(pipe, msg, msglen)) {
        D("can't write qemud frame payload: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static __inline__ int
qemud_channel_recv(QEMU_PIPE_HANDLE pipe, void*  msg, int  msgsize)
{
    char  header[5];
    int   size;

    if (qemu_pipe_read_fully(pipe, header, 4)) {
        D("can't read qemud frame header: %s", strerror(errno));
        return -1;
    }
    header[4] = 0;
    if (sscanf(header, "%04x", &size) != 1) {
        D("malformed qemud frame header: '%.*s'", 4, header);
        return -1;
    }
    if (size > msgsize)
        return -1;

    if (qemu_pipe_read_fully(pipe, msg, size)) {
        D("can't read qemud frame payload: %s", strerror(errno));
        return -1;
    }
    return size;
}

#endif /* ANDROID_INCLUDE_HARDWARE_QEMUD_H */
