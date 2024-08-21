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
#include "PlaneLayout.h"

struct CbExternalMetadata {
    static constexpr uint64_t kMagicValue = 0x247439A87E42E932LLU;

    struct Smpte2086 {
        struct XyColor {
            float x;
            float y;
        };

        XyColor primaryRed;
        XyColor primaryGreen;
        XyColor primaryBlue;
        XyColor whitePoint;
        float maxLuminance;
        float minLuminance;
    };

    struct Cta861_3 {
        float maxContentLightLevel;
        float maxFrameAverageLightLevel;
    };

    uint64_t    magic;
    uint64_t    bufferID;
    PlaneLayout planeLayout[3];
    PlaneLayoutComponent planeLayoutComponent[4];
    Smpte2086   smpte2086;
    Cta861_3    cta861_3;
    uint32_t    width;              // buffer width
    uint32_t    height;             // buffer height
    int32_t     glFormat;           // OpenGL format enum used for host h/w color buffer
    int32_t     glType;             // OpenGL type enum used when uploading to host
    uint32_t    reservedRegionSize;
    int32_t     dataspace;
    int32_t     blendMode;

    uint8_t     planeLayoutSize;
    uint8_t     nameSize;
    bool        has_smpte2086;
    bool        has_cta861_3;

    char        name[127];
    char        unused[1];
};

static_assert((sizeof(CbExternalMetadata) % 16) == 0);
