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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <qemud.h>
#include <unistd.h>

namespace {
// TODO(rkir): move these functions to libqemupipe.ranchu

bool qemu_pipe_try_again(int ret) {
    return (ret < 0) && (errno == EINTR || errno == EAGAIN);
}

#define QEMU_PIPE_RETRY(exp) ({ \
    __typeof__(exp) _rc; \
    do { \
        _rc = (exp); \
    } while (qemu_pipe_try_again(_rc)); \
    _rc; })

int qemu_pipe_read_fully(int pipe, void* buffer, int len) {
    char* p = (char*)buffer;
    while (len > 0) {
      int n = QEMU_PIPE_RETRY(read(pipe, p, len));
      if (n < 0) return n;
      p += n;
      len -= n;
    }
    return 0;
}

int qemu_pipe_write_fully(int pipe, const void* buffer, int len) {
    const char* p = (const char*)buffer;
    while (len > 0) {
      int n = QEMU_PIPE_RETRY(write(pipe, p, len));
      if (n < 0) return n;
      p += n;
      len -= n;
    }
    return 0;
}

int qemu_pipe_open_ns(const char* ns, const char* pipeName, const int flags) {
    int fd = QEMU_PIPE_RETRY(open("/dev/goldfish_pipe", flags));
    if (fd < 0) {
        return -1;
    }
    char buff[64];
    int len = snprintf(buff, sizeof(buff), "pipe:%s:%s", ns, pipeName);
    if (qemu_pipe_write_fully(fd, buff, len + 1)) {
        close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

int qemud_channel_open(const char*  name) {
    return qemu_pipe_open_ns("qemud", name, O_RDWR);
}

int qemud_channel_send(int pipe, const void* msg, int size) {
    char header[5];

    if (size < 0)
        size = strlen((const char*)msg);

    if (size == 0)
        return 0;

    snprintf(header, sizeof(header), "%04x", size);
    if (qemu_pipe_write_fully(pipe, header, 4)) {
        return -1;
    }

    if (qemu_pipe_write_fully(pipe, msg, size)) {
        return -1;
    }

    return 0;
}

int qemud_channel_recv(int pipe, void* msg, int maxsize) {
    char header[5];
    int  size;

    if (qemu_pipe_read_fully(pipe, header, 4)) {
        return -1;
    }
    header[4] = 0;

    if (sscanf(header, "%04x", &size) != 1) {
        return -1;
    }
    if (size > maxsize) {
        return -1;
    }

    if (qemu_pipe_read_fully(pipe, msg, size)) {
        return -1;
    }

    return size;
}
