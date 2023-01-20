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

#define FAILURE_DEBUG_PREFIX "exif"

#include "exif.h"
#include "debug.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {
namespace exif {
namespace {

struct ExifMemDeleter {
    void operator()(ExifMem* allocator) const {
        exif_mem_unref(allocator);
    }
};

typedef std::unique_ptr<ExifMem, ExifMemDeleter> ExifMemPtr;

}  // namespace

ExifDataPtr createExifData(const CameraMetadata& /*metadata*/,
                           const Rect<uint16_t> /*size*/) {
    ExifMemPtr allocator(exif_mem_new_default());
    ExifDataPtr exifData(exif_data_new_mem(allocator.get()));

    exif_data_set_option(exifData.get(), EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
    exif_data_set_data_type(exifData.get(), EXIF_DATA_TYPE_COMPRESSED);
    exif_data_set_byte_order(exifData.get(), EXIF_BYTE_ORDER_INTEL);
    exif_data_fix(exifData.get());

    // TODO

    return exifData;
}

void* exifDataAllocThumbnail(ExifData* const edata, const size_t size) {
    // WARNING: maloc and free must match the functions that are used in
    // exif_mem_new_default (see above) to manage memory. They will be used in
    // exif_data_free to deallocate memory allocated here.
    void* mem = malloc(size);
    if (mem) {
        if (edata->data) {
            free(edata->data);
        }
        edata->size = size;
        edata->data = static_cast<uint8_t*>(mem);
    }
    return mem;
}

void ExifDataDeleter::operator()(ExifData* p) const {
    exif_data_free(p);
}

}  // namespace exif
}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
