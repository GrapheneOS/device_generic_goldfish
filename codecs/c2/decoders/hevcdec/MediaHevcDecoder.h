/*
 * Copyright 2022 The Android Open Source Project
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

#include "goldfish_media_utils.h"

namespace goldfish::media::c2 {

class MediaHevcDecoder {
  public:
    MediaHevcDecoder(RenderMode renderMode);
    virtual ~MediaHevcDecoder() = default;

    enum class PixelFormat : uint8_t {
        YUV420P = 0,
        UYVY422 = 1,
        BGRA8888 = 2,
    };

    enum class Err : int {
        NoErr = 0,
        NoDecodedFrame = -1,
        InitContextFailed = -2,
        DecoderRestarted = -3,
        NALUIgnored = -4,
    };

    bool getAddressSpaceMemory();
    void initHevcContext(unsigned int width, unsigned int height,
                         unsigned int outWidth, unsigned int outHeight,
                         PixelFormat pixFmt);
    void resetHevcContext(unsigned int width, unsigned int height,
                          unsigned int outWidth, unsigned int outHeight,
                          PixelFormat pixFmt);
    void destroyHevcContext();
    GfResult decodeFrame(uint8_t *img, size_t szBytes, uint64_t pts);
    void flush();
    // ask host to copy image data back to guest, with image metadata
    // to guest as well
    GfImage getImage();
    // ask host to render to hostColorBufferId, return only image metadata back
    // to guest
    GfImage renderOnHostAndReturnImageMetadata(int hostColorBufferId);

    void sendMetadata(MetaDataColorAspects *ptr);

  private:
    uint64_t mHostHandle = 0;
    uint64_t mAddressOffSet = 0;
    uint32_t mVersion = 100;
    int mSlot = -1;
    bool mHasAddressSpaceMemory = false;
};

}  // namespace goldfish::media::c2
