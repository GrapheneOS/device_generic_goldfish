/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "multihal_sensors_transport.h"

namespace goldfish {

QemudSensorsTransport::QemudSensorsTransport(const char* name)
    : m_qemuSensorsFd(qemud_channel_open(name)) {}

int QemudSensorsTransport::Send(const void* msg, int size) {
    return qemud_channel_send(m_qemuSensorsFd.get(), msg, size);
}

int QemudSensorsTransport::Receive(void* msg, int maxsize) {
    return qemud_channel_recv(m_qemuSensorsFd.get(), msg, maxsize);
}

bool QemudSensorsTransport::Ok() const {
    return m_qemuSensorsFd.ok();
}

int QemudSensorsTransport::Fd() const {
    return m_qemuSensorsFd.get();
}

const char* QemudSensorsTransport::Name() const {
    return "qemud_channel";
}

}  // namespace goldfish
