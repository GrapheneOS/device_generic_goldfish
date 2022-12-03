
/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef GOLDFISH_CAMERA_EXIF_H
#define GOLDFISH_CAMERA_EXIF_H

#include <memory>
#include <libexif/exif-data.h>
#undef TRUE
#undef FALSE
#include <CameraParameters.h>
#include <CameraMetadata.h>

namespace android {
struct ExifDataDeleter { void operator()(ExifData*) const; };
typedef std::unique_ptr<ExifData, ExifDataDeleter> ExifDataPtr;

using ::android::hardware::camera::common::V1_0::helper::CameraParameters;
using ::android::hardware::camera::common::V1_0::helper::CameraMetadata;

/* Create an EXIF data structure based on camera parameters. This includes
 * things like GPS information that has been set by the camera client.
 * First for Camera HAL1 and the second for Camera HAL3.
 */
ExifDataPtr createExifData(const CameraParameters& parameters);
ExifDataPtr createExifData(const CameraMetadata& params, int width, int height);

}  // namespace android

#endif  // GOLDFISH_CAMERA_EXIF_H

