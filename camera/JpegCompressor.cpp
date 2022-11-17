/*
 * Copyright (C) 2011 The Android Open Source Project
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

/*
 * Contains implementation of a class NV21JpegCompressor that encapsulates a
 * converter between NV21, and JPEG formats.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_JPEG"
#include <log/log.h>
#include "JpegCompressor.h"

namespace android {

/****************************************************************************
 * Public API
 ***************************************************************************/

status_t NV21JpegCompressor::compressRawImage(const void* image,
                                              int width,
                                              int height,
                                              int quality,
                                              const ExifData* exifData) {
    if (mStub.compress(image, width, height, quality, exifData)) {
        ALOGV("%s: Compressed JPEG: %d[%dx%d] -> %zu bytes",
              __func__, (width * height * 12) / 8,
              width, height, mStub.getCompressedData().size());

        return 0;
    } else {
        int const err = errno ? errno : EINVAL;
        ALOGE("%s: JPEG compression failed with %d", __func__, err);
        return err;
    }
}

size_t NV21JpegCompressor::getCompressedSize() const {
    return mStub.getCompressedData().size();
}

void NV21JpegCompressor::getCompressedImage(void* dst) const {
    const auto data = mStub.getCompressedData();
    memcpy(dst, data.data(), data.size());
}

} /* namespace android */
