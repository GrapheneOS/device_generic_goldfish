/*
* Copyright 2024 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#pragma once
#include <cstdint>

struct PlaneLayoutComponent {
    uint32_t type; // see PlaneLayoutComponentType
    uint16_t offsetInBits;
    uint16_t sizeInBits;
};

struct PlaneLayout {
    uint32_t offsetInBytes;
    uint32_t strideInBytes;
    uint32_t totalSizeInBytes;
    uint8_t sampleIncrementInBytes;
    uint8_t horizontalSubsamplingShift : 4;
    uint8_t verticalSubsamplingShift : 4;
    uint8_t componentsBase; // in the PlaneLayoutComponent array
    uint8_t componentsSize;
};
