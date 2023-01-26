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

#include <hardware/camera3.h>

#include "HwCamera.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {
namespace hw {
namespace {
constexpr float kDefaultAperture = 4.0;
constexpr float kDefaultFocalLength = 1.0;
constexpr int32_t kDefaultSensorSensitivity = 100;
}  // namespace

std::tuple<int32_t, int32_t, int32_t, int32_t> HwCamera::getAeCompensationRange() const {
    return {-6, 6, 1, 2}; // range=[-6, +6], step=1/2
}

std::pair<float, float> HwCamera::getZoomRatioRange() const {
    return {1.0, 1.0};
}

std::pair<int, int> HwCamera::getSupportedFlashStrength() const {
    return {0, 0};
}

int32_t HwCamera::getJpegMaxSize() const {
    const Rect<uint16_t> size = getSensorSize();
    return int32_t(size.width) * int32_t(size.height) + sizeof(camera3_jpeg_blob);
}

Span<const float> HwCamera::getAvailableApertures() const {
    static const float availableApertures[] = {
        kDefaultAperture
    };

    return availableApertures;
}

Span<const float> HwCamera::getAvailableFocalLength() const {
    static const float availableFocalLengths[] = {
        kDefaultFocalLength
    };

    return availableFocalLengths;
}

float HwCamera::getHyperfocalDistance() const {
    return 0.1;
}

float HwCamera::getMinimumFocusDistance() const {
    return 0.1;
}

int32_t HwCamera::getPipelineMaxDepth() const {
    return 4;
}

float HwCamera::getMaxDigitalZoom() const {
    return 1.0;
}

int64_t HwCamera::getStallFrameDurationNs() const {
    return 250000000LL;
}

int32_t HwCamera::getSensorOrientation() const {
    return 0;
}

float HwCamera::getSensorDPI() const {
    return 500.0;
}

std::pair<int32_t, int32_t> HwCamera::getSensorSensitivityRange() const {
    return {kDefaultSensorSensitivity, kDefaultSensorSensitivity};
}

float HwCamera::getDefaultAperture() const {
    return kDefaultAperture;
}

float HwCamera::getDefaultFocalLength() const {
    return kDefaultFocalLength;
}

int32_t HwCamera::getDefaultSensorSensitivity() const {
    return kDefaultSensorSensitivity;
}

}  // namespace hw
}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
