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

#pragma once

namespace goldfish {

enum SensorsMessageType {
    // Sensors HAL handshake and control messages.
    CONTROL = 0,
    // High-frequency sensors data reported from host.
    DATA
};

class SensorsTransport {
 public:
    virtual int Send(SensorsMessageType type, const void* msg, int size) = 0;
    virtual int Receive(SensorsMessageType type, void* msg, int maxsize) = 0;
    virtual bool Ok() const = 0;
    virtual int Fd(SensorsMessageType type) const = 0;
    virtual const char* Name() const = 0;

    virtual ~SensorsTransport() = default;
};

}  // namespace goldfish
