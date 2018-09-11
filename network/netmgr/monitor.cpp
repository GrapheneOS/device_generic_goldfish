/*
 * Copyright 2018, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "monitor.h"

#include "log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

Monitor::Monitor() : mSocketFd(-1) {

}

Monitor::~Monitor() {
    closeSocket();
}

Result Monitor::init() {
    return openSocket();
}

void Monitor::setOnInterfaceState(OnInterfaceStateCallback callback) {
    mOnInterfaceStateCallback = callback;
}

void Monitor::onReadAvailable() {
    char buffer[32768];
    struct sockaddr_storage storage;

    while (true) {
        socklen_t addrSize = sizeof(storage);
        int status = ::recvfrom(mSocketFd,
                                buffer,
                                sizeof(buffer),
                                MSG_DONTWAIT,
                                reinterpret_cast<struct sockaddr*>(&storage),
                                &addrSize);
        if (status < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Nothing to receive, everything is fine
            return;
        } else if (status < 0) {
            LOGE("Monitor receive failed: %s", strerror(errno));
            return;
        } else if (addrSize < 0 ||
                   static_cast<size_t>(addrSize) != sizeof(struct sockaddr_nl)) {
            LOGE("Monitor received invalid address size");
            return;
        }

        size_t length = static_cast<size_t>(status);

        auto hdr = reinterpret_cast<struct nlmsghdr*>(buffer);
        while (NLMSG_OK(hdr, length) && hdr->nlmsg_type != NLMSG_DONE) {
            switch (hdr->nlmsg_type) {
                case RTM_NEWLINK:
                    handleNewLink(hdr);
                    break;
                default:
                    break;
            }
            NLMSG_NEXT(hdr, length);
        }
    }
}

void Monitor::onClose() {
    // Socket was closed from the other end, close it from our end and re-open
    closeSocket();
    Result res = openSocket();
    if (!res) {
        LOGE("%s", res.c_str());
    }
}

void Monitor::onTimeout() {
}

Pollable::Data Monitor::data() const {
    return Pollable::Data{ mSocketFd, Pollable::Timestamp::max() };
}

Result Monitor::openSocket() {
    if (mSocketFd != -1) {
        return Result::error("Monitor already initialized");
    }

    mSocketFd = ::socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (mSocketFd == -1) {
        return Result::error("Monitor failed to open socket: %s",
                             strerror(errno));
    }

    struct sockaddr_nl addr;
    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTNLGRP_LINK | RTNLGRP_IPV4_IFADDR | RTNLGRP_IPV6_IFADDR;

    struct sockaddr* sa = reinterpret_cast<struct sockaddr*>(&addr);
    if (::bind(mSocketFd, sa, sizeof(addr)) != 0) {
        return Result::error("Monitor failed to bind socket: %s",
                             strerror(errno));
    }

    return Result::success();
}

void Monitor::closeSocket() {
    if (mSocketFd != -1) {
        ::close(mSocketFd);
        mSocketFd = -1;
    }
}

void Monitor::handleNewLink(const struct nlmsghdr* hdr) {
    if (!mOnInterfaceStateCallback) {
        return;
    }

    auto msg = reinterpret_cast<const struct ifinfomsg*>(NLMSG_DATA(hdr));

    if (msg->ifi_change & IFF_UP) {
        // The interface up/down flag changed, send a notification
        char name[IF_NAMESIZE + 1] = { 0 };
        if_indextoname(msg->ifi_index, name);

        InterfaceState state = (msg->ifi_flags & IFF_UP) ? InterfaceState::Up :
                                                           InterfaceState::Down;
        mOnInterfaceStateCallback(msg->ifi_index, name, state);
    }
}

