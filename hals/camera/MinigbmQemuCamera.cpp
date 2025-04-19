/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <string>
#include <string_view>

#include <linux/videodev2.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>

#include "MinigbmQemuCamera.h"

#include "debug.h"
#include "jpeg.h"
#include "qemu_channel.h"
#include "yuv.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {
namespace hw {
using namespace std::literals;

namespace {
constexpr char kClass[] = "MinigbmQemuCamera";

constexpr uint64_t kDelayedBufferAllocUsage =
    static_cast<uint64_t>(BufferUsage::CAMERA_OUTPUT) |
    static_cast<uint64_t>(BufferUsage::CPU_READ_OFTEN);

}  // namespace

MinigbmQemuCamera::MinigbmQemuCamera(const Parameters& params)
        : BaseQemuCamera(params)
        , mGfxGralloc(gfxstream::createPlatformGralloc())
{}

bool MinigbmQemuCamera::configure(const CameraMetadata& sessionParams,
                                  size_t nStreams,
                                  const Stream* streams,
                                  const HalStream* halStreams) {
    constexpr std::string_view kConfigureQueryPrefix = "configure streams="sv;

    std::string query;
    query.reserve(kConfigureQueryPrefix.size() + 30U * nStreams);
    query.append(kConfigureQueryPrefix);

    std::vector<StreamInfo> newStreams(nStreams);
    for (size_t i = 0; i < nStreams; ++i, ++streams, ++halStreams) {
        LOG_ALWAYS_FATAL_IF(streams->id != halStreams->id);
        StreamInfo& si = newStreams[i];
        si.id = streams->id;
        si.size.width = streams->width;
        si.size.height = streams->height;
        si.blobBufferSize = streams->bufferSize;
        si.format = halStreams->overrideFormat;

        PixelFormat hostFormat;
        switch (si.format) {
        case PixelFormat::BLOB:
            hostFormat = PixelFormat::YCBCR_420_888;
            break;

        case PixelFormat::RAW16:
            hostFormat = PixelFormat::RGBA_8888;
            break;

        default:
            hostFormat = si.format;
            break;
        }

        char buf[64];
        const int len =
            ::snprintf(buf, sizeof(buf), "%s%d:%ux%u@%X",
                       ((i > 0) ? "," : ""), si.id,
                       si.size.width, si.size.height,
                       static_cast<uint32_t>(hostFormat));
        query.append(buf, len);
    }

    if (!mQemuChannel.ok()) {
        auto qemuChannel = qemuOpenChannel(std::string("name="sv) + mParams.name);
        if (qemuChannel.ok()) {
            mQemuChannel = std::move(qemuChannel);
        } else {
            return false;
        }
    }

    if (qemuRunQuery(mQemuChannel.get(), query.data(), query.size() + 1) < 0) {
        mQemuChannel.reset();
        return false;
    }

    mStreams = std::move(newStreams);
    applyMetadata(sessionParams);
    return true;
}

void MinigbmQemuCamera::close() {
    mQemuChannel.reset();
    mStreams.clear();
}

std::tuple<int64_t, int64_t, CameraMetadata,
           std::vector<StreamBuffer>, std::vector<DelayedStreamBuffer>>
MinigbmQemuCamera::processCaptureRequest(CameraMetadata metadataUpdate,
                                         Span<CachedStreamBuffer*> csbs) {
    constexpr std::string_view kCaptureQueryPrefix = "capture bufs="sv;

    CameraMetadata resultMetadata = metadataUpdate.metadata.empty() ?
        updateCaptureResultMetadata() :
        applyMetadata(std::move(metadataUpdate));

    const size_t csbsSize = csbs.size();

    std::string query;
    query.reserve(kCaptureQueryPrefix.size() + 10U * csbsSize);
    query.append(kCaptureQueryPrefix);

    std::vector<CachedStreamBuffer*> immediateBuffers;
    std::vector<StreamBuffer> outputBuffers;
    std::vector<DelayedStreamBuffer> delayedOutputBuffers;

    immediateBuffers.reserve(csbsSize);
    outputBuffers.reserve(csbsSize);

    bool firstEntry = true;
    for (size_t i = 0; i < csbsSize; ++i) {
        CachedStreamBuffer* csb = csbs[i];
        LOG_ALWAYS_FATAL_IF(!csb);  // otherwise mNumBuffersInFlight will be hard

        const native_handle_t* captureBuf;
        const StreamInfo* si = csb->getStreamInfo<StreamInfo>();
        if (!si) {
            const int32_t id = csb->getStreamId();
            const auto sii =
                std::find_if(mStreams.begin(), mStreams.end(),
                             [id](const StreamInfo& si){
                                 return id == si.id;
                             });

            if (sii == mStreams.end()) {
                ALOGE("%s:%s:%d: could not find stream=%d in the cache",
                      kClass, __func__, __LINE__, id);
                goto failCsb;
            } else {
                si = &*sii;
                csb->setStreamInfo(si);
            }
        }

        switch (si->format) {
        case PixelFormat::BLOB: {
                const Rect<uint16_t> imageSize = si->size;
                GraphicBufferAllocator& gba = GraphicBufferAllocator::get();
                uint32_t stride;
                if (gba.allocate(imageSize.width, imageSize.height,
                                 static_cast<int>(PixelFormat::YCBCR_420_888), 1,
                                 kDelayedBufferAllocUsage, &captureBuf, &stride,
                                 "MinigbmQemuCamera") == NO_ERROR) {
                    CameraMetadata metadata = mCaptureResultMetadata;
                    const size_t jpegBufferSize = si->blobBufferSize;
                    delayedOutputBuffers.push_back([captureBuf, csb, imageSize, jpegBufferSize,
                                                    metadata = std::move(metadata)]
                                                   (const bool ok) -> StreamBuffer {
                        StreamBuffer sb;
                        if (ok && csb->waitAcquireFence(100)) {
                            android_ycbcr imageYcbcr;
                            if (GraphicBufferMapper::get().lockYCbCr(
                                    captureBuf, static_cast<uint32_t>(BufferUsage::CPU_READ_OFTEN),
                                    {imageSize.width, imageSize.height}, &imageYcbcr) == NO_ERROR) {
                                sb = csb->finish(compressJpeg(imageSize, imageYcbcr, metadata,
                                                              csb->getBuffer(), jpegBufferSize));
                                LOG_ALWAYS_FATAL_IF(GraphicBufferMapper::get().unlock(captureBuf) != NO_ERROR);
                            } else {
                                sb = csb->finish(FAILURE(false));
                            }
                        } else {
                            sb = csb->finish(false);
                        }

                        GraphicBufferAllocator::get().free(captureBuf);
                        return sb;
                    });
                } else {
                    captureBuf = nullptr;
                }
            }
            break;

        case PixelFormat::RAW16: {
                const Rect<uint16_t> imageSize = si->size;
                GraphicBufferAllocator& gba = GraphicBufferAllocator::get();
                uint32_t stride;
                if (gba.allocate(imageSize.width, imageSize.height,
                                 static_cast<int>(PixelFormat::RGBA_8888), 1,
                                 kDelayedBufferAllocUsage, &captureBuf, &stride,
                                 "MinigbmQemuCamera") == NO_ERROR) {
                    CameraMetadata metadata = mCaptureResultMetadata;
                    delayedOutputBuffers.push_back([captureBuf, csb, imageSize,
                                                    metadata = std::move(metadata)]
                                                   (const bool ok) -> StreamBuffer {
                        StreamBuffer sb;
                        if (ok && csb->waitAcquireFence(100)) {
                            void* mem = nullptr;
                            if (GraphicBufferMapper::get().lock(
                                    captureBuf, static_cast<uint32_t>(BufferUsage::CPU_READ_OFTEN),
                                    {imageSize.width, imageSize.height}, &mem) == NO_ERROR) {
                                sb = csb->finish(convertRGBAtoRAW16(imageSize, mem, csb->getBuffer()));
                                LOG_ALWAYS_FATAL_IF(GraphicBufferMapper::get().unlock(captureBuf) != NO_ERROR);
                            } else {
                                sb = csb->finish(FAILURE(false));
                            }
                        } else {
                            sb = csb->finish(false);
                        }

                        GraphicBufferAllocator::get().free(captureBuf);
                        return sb;
                    });
                } else {
                    captureBuf = nullptr;
                }
            }
            break;

        default:
            immediateBuffers.push_back(csb);
            captureBuf = csb->getBuffer();
            break;
        }

        if (captureBuf) {
            const uint32_t hostHandle = mGfxGralloc->getHostHandle(captureBuf);

            char buf[32];
            const int len =
                ::snprintf(buf, sizeof(buf), "%s%d:%u", (firstEntry ? "" : ","),
                           si->id, hostHandle);
            query.append(buf, len);
            firstEntry = false;
        } else {
failCsb:    outputBuffers.push_back(csb->finish(false));
        }
    }

    if (qemuRunQuery(mQemuChannel.get(), query.data(), query.size() + 1) >= 0) {
        for (CachedStreamBuffer* csb : immediateBuffers) {
            outputBuffers.push_back(csb->finish(true));
        }
    } else {
        for (CachedStreamBuffer* csb : immediateBuffers) {
            outputBuffers.push_back(csb->finish(false));
        }

        for (const DelayedStreamBuffer& dsb : delayedOutputBuffers) {
            outputBuffers.push_back(dsb(false));
            delayedOutputBuffers.clear();
        }
    }

    return make_tuple((mQemuChannel.ok() ? mFrameDurationNs : FAILURE(-1)),
                      mSensorExposureDurationNs,
                      std::move(resultMetadata), std::move(outputBuffers),
                      std::move(delayedOutputBuffers));
}

}  // namespace hw
}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
