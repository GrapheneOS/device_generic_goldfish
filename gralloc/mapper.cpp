/*
* Copyright (C) 2024 The Android Open Source Project
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

#include <array>
#include <mutex>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <cutils/native_handle.h>
#include <log/log.h>
#include <sync/sync.h>

#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/ChromaSiting.h>
#include <aidl/android/hardware/graphics/common/Compression.h>
#include <aidl/android/hardware/graphics/common/Interlaced.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
#include <aidl/android/hardware/graphics/common/StandardMetadataType.h>

#include <android/hardware/graphics/mapper/IMapper.h>
#include <android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h>

#include <debug.h>
#include <FormatConversions.h>
#include <goldfish_address_space.h>
#include <gralloc_cb_bp.h>

#include "CbExternalMetadata.h"
#include "DebugLevel.h"
#include "HostConnectionSession.h"

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif

namespace aahgc = ::aidl::android::hardware::graphics::common;
using aahgc::BufferUsage;
using aahgc::ChromaSiting;
using aahgc::Interlaced;
using aahgc::PixelFormat;
using aahgc::StandardMetadataType;

using ::android::hardware::graphics::mapper::MetadataReader;
using ::android::hardware::graphics::mapper::MetadataWriter;

namespace {
constexpr size_t kMetadataBufferInitialSize = 1024;
constexpr uint32_t kCPU_READ_MASK = static_cast<uint32_t>(BufferUsage::CPU_READ_MASK);
constexpr uint32_t kCPU_WRITE_MASK = static_cast<uint32_t>(BufferUsage::CPU_WRITE_MASK);

using namespace std::literals;

const char kStandardMetadataTypeStr[] = "android.hardware.graphics.common.StandardMetadataType";
const std::string_view kStandardMetadataTypeTag(kStandardMetadataTypeStr, sizeof(kStandardMetadataTypeStr) - 1);
const std::string_view kChromaSitingTag = "android.hardware.graphics.common.ChromaSiting"sv;
const std::string_view kCompressionTag = "android.hardware.graphics.common.Compression"sv;
const std::string_view kInterlacedTag = "android.hardware.graphics.common.Interlaced"sv;
const std::string_view kPlaneLayoutComponentTypeTag = "android.hardware.graphics.common.PlaneLayoutComponentType"sv;

template<class T, size_t SIZE> constexpr size_t arraySize(T (&)[SIZE]) { return SIZE; }

PixelFormat getPixelFormat(const cb_handle_t& cb) {
    return static_cast<PixelFormat>(cb.format);
}

bool isYuvFormat(const PixelFormat format) {
    switch (format) {
    case PixelFormat::YCRCB_420_SP:
    case PixelFormat::YV12:
    case PixelFormat::YCBCR_420_888:
    case PixelFormat::YCBCR_P010:
        return true;

    default:
        return false;
    }
}

ChromaSiting getFormatChromaSiting(const PixelFormat format) {
    return isYuvFormat(format) ? ChromaSiting::SITED_INTERSTITIAL : ChromaSiting::NONE;
}

CbExternalMetadata& getExternalMetadata(const cb_handle_t& cb) {
    CbExternalMetadata& m = *reinterpret_cast<CbExternalMetadata*>(
        cb.getBufferPtr() + cb.externalMetadataOffset);
    LOG_ALWAYS_FATAL_IF(m.magic != CbExternalMetadata::kMagicValue);
    return m;
}

uint64_t getID(const cb_handle_t& cb) {
    return getExternalMetadata(cb).bufferID;
}

int waitFenceFd(const int fd, const char* logname) {
    const int warningTimeout = 5000;
    if (sync_wait(fd, warningTimeout) < 0) {
        if (errno == ETIME) {
            ALOGW("%s: fence %d didn't signal in %d ms", logname, fd, warningTimeout);
            if (sync_wait(fd, -1) < 0) {
                return errno;
            } else {
                return 0;
            }
        } else {
            return errno;
        }
    } else {
        return 0;
    }
}

const AIMapper_MetadataTypeDescription kMetadataTypeDescriptionList[] = {
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::BUFFER_ID),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::NAME),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::WIDTH),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::HEIGHT),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::LAYER_COUNT),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::PIXEL_FORMAT_REQUESTED),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::PIXEL_FORMAT_FOURCC),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::PIXEL_FORMAT_MODIFIER),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::USAGE),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::ALLOCATION_SIZE),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::PROTECTED_CONTENT),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::COMPRESSION),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::INTERLACED),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::CHROMA_SITING),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::PLANE_LAYOUTS),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::CROP),
        },
        .isGettable = true,
        .isSettable = false,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::DATASPACE),
        },
        .isGettable = true,
        .isSettable = true,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::BLEND_MODE),
        },
        .isGettable = true,
        .isSettable = true,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::SMPTE2086),
        },
        .isGettable = true,
        .isSettable = true,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::CTA861_3),
        },
        .isGettable = true,
        .isSettable = true,
    },
    {
        .metadataType = {
            .name = kStandardMetadataTypeStr,
            .value = static_cast<int64_t>(StandardMetadataType::STRIDE),
        },
        .isGettable = true,
        .isSettable = false,
    },
};

struct GoldfishMapper {
    GoldfishMapper()
            : mHostConn(HostConnection::createUnique(kCapsetNone))
            , mDebugLevel(getDebugLevel()) {
        GoldfishAddressSpaceHostMemoryAllocator hostMemoryAllocator(false);
        LOG_ALWAYS_FATAL_IF(!hostMemoryAllocator.is_opened(),
            "GoldfishAddressSpaceHostMemoryAllocator failed to open");

        GoldfishAddressSpaceBlock bufferBits;
        LOG_ALWAYS_FATAL_IF(hostMemoryAllocator.hostMalloc(&bufferBits, 256),
                            "hostMalloc failed");

        mPhysAddrToOffset = bufferBits.physAddr() - bufferBits.offset();
        hostMemoryAllocator.hostFree(&bufferBits);

        static GoldfishMapper* s_instance;

        mMapper.version = AIMAPPER_VERSION_5;
        mMapper.v5.importBuffer = [](const native_handle_t* handle,
                                     buffer_handle_t* outBufferHandle) {
            return s_instance->importBuffer(handle, outBufferHandle);
        };
        mMapper.v5.freeBuffer = [](buffer_handle_t buffer) {
            return s_instance->freeBuffer(buffer);
        };
        mMapper.v5.getTransportSize = &getTransportSize;
        mMapper.v5.lock = [](buffer_handle_t buffer, uint64_t cpuUsage,
                             ARect accessRegion, int acquireFence,
                             void** outData){
            return s_instance->lock(buffer, cpuUsage, accessRegion,
                                    acquireFence, outData);
        };
        mMapper.v5.unlock = [](buffer_handle_t buffer, int* releaseFence) {
            return s_instance->unlock(buffer, releaseFence);
        };
        mMapper.v5.flushLockedBuffer = [](buffer_handle_t buffer) {
            return s_instance->flushLockedBuffer(buffer);
        };
        mMapper.v5.rereadLockedBuffer = [](buffer_handle_t buffer) {
            return s_instance->rereadLockedBuffer(buffer);
        };
        mMapper.v5.getMetadata = [](const buffer_handle_t buffer,
                                    const AIMapper_MetadataType metadataType,
                                    void* const destBuffer, const size_t destBufferSize) {
            return s_instance->getMetadata(buffer, metadataType,
                                           destBuffer, destBufferSize);
        };
        mMapper.v5.getStandardMetadata = [](const buffer_handle_t buffer,
                                            const int64_t standardMetadataType,
                                            void* const destBuffer,
                                            const size_t destBufferSize) {
            return s_instance->getStandardMetadata(buffer, standardMetadataType,
                                                   destBuffer, destBufferSize);
        };
        mMapper.v5.setMetadata = [](const buffer_handle_t buffer,
                                    const AIMapper_MetadataType metadataType,
                                    const void* const metadata, const size_t metadataSize) {
            return s_instance->setMetadata(buffer, metadataType,
                                           metadata, metadataSize);
        };
        mMapper.v5.setStandardMetadata = [](const buffer_handle_t buffer,
                                            const int64_t standardMetadataType,
                                            const void* const metadata,
                                            const size_t metadataSize) {
            return s_instance->setStandardMetadata(buffer, standardMetadataType,
                                                   metadata, metadataSize);
        };
        mMapper.v5.listSupportedMetadataTypes = &listSupportedMetadataTypes;
        mMapper.v5.dumpBuffer = [](const buffer_handle_t buffer,
                                   const AIMapper_DumpBufferCallback dumpBufferCallback,
                                   void* const context) {
            return s_instance->dumpBuffer(buffer, dumpBufferCallback, context);
        };
        mMapper.v5.dumpAllBuffers = [](AIMapper_BeginDumpBufferCallback beginDumpCallback,
                                       AIMapper_DumpBufferCallback dumpBufferCallback,
                                       void* context){
            return s_instance->dumpAllBuffers(beginDumpCallback, dumpBufferCallback,
                                              context);
        };
        mMapper.v5.getReservedRegion = [](const buffer_handle_t buffer,
                                          void** const outReservedRegion,
                                          uint64_t* const outReservedSize) {
            return s_instance->getReservedRegion(buffer, outReservedRegion,
                                                 outReservedSize);
        };

        s_instance = this;
    }

    AIMapper& getAIMapper() {
        return mMapper;
    }

private:
    AIMapper_Error importBuffer(const native_handle_t* const handle,
                                buffer_handle_t* const outBufferHandle) {
        if (!handle) {
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }
        native_handle_t* const imported = native_handle_clone(handle);
        if (!imported) {
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }
        cb_handle_t* const cb = cb_handle_t::from(imported);
        if (!cb) {
            native_handle_close(imported);
            native_handle_delete(imported);
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }

        if (cb->mmapedSize) {
            const int bufferFd = cb->bufferFd;
            LOG_ALWAYS_FATAL_IF(bufferFd < 0);

            void* newPtr;
            const int err = GoldfishAddressSpaceBlock::memoryMap(
                cb->getBufferPtr(), cb->mmapedSize,
                bufferFd, cb->getMmapedOffset(), &newPtr);
            if (err) {
                native_handle_close(imported);
                native_handle_delete(imported);
                return FAILURE_V(AIMAPPER_ERROR_NO_RESOURCES, "%s: %s",
                                 "NO_RESOURCES", strerror(err));
            }
            cb->setBufferPtr(newPtr);
        }

        if (mDebugLevel >= DebugLevel::IMPORT) {
            ALOGD("%s:%d: id=%" PRIu64, __func__, __LINE__, getID(*cb));
        }

        std::lock_guard<std::mutex> lock(mImportedBuffersMtx);
        LOG_ALWAYS_FATAL_IF(!mImportedBuffers.insert(cb).second);
        *outBufferHandle = cb;
        return AIMAPPER_ERROR_NONE;
    }

    AIMapper_Error freeBuffer(buffer_handle_t buffer) {
        cb_handle_t* const cb = const_cast<cb_handle_t*>(static_cast<const cb_handle_t*>(buffer));

        {
            std::lock_guard<std::mutex> lock(mImportedBuffersMtx);
            if (mImportedBuffers.erase(cb) == 0) {
                return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
            }
        }

        if (mDebugLevel >= DebugLevel::IMPORT) {
            ALOGD("%s:%d: id=%" PRIu64, __func__, __LINE__, getID(*cb));
        }

        if (cb->hostHandle && (cb->lockedUsage & kCPU_WRITE_MASK)) {
            flushToHost(*cb);
        }
        GoldfishAddressSpaceBlock::memoryUnmap(cb->getBufferPtr(),
                                               cb->mmapedSize);
        native_handle_close(cb);
        native_handle_delete(cb);
        return AIMAPPER_ERROR_NONE;
    }

    static AIMapper_Error getTransportSize(const buffer_handle_t buffer,
                                           uint32_t* const outNumFds,
                                           uint32_t* const outNumInts) {
        const cb_handle_t* const cb = cb_handle_t::from(buffer);
        if (!cb) {
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }

        *outNumFds = cb->numFds;
        *outNumInts = cb->numInts;
        return AIMAPPER_ERROR_NONE;
    }

    AIMapper_Error lock(const buffer_handle_t buffer, const uint64_t uncheckedUsage,
                        const ARect& accessRegion, const int acquireFence,
                        void** const outData) const {
        cb_handle_t* const cb = validateCb(buffer);
        if (!cb) {
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }

        const CbExternalMetadata& metadata = getExternalMetadata(*cb);
        if (cb->lockedUsage) {
            return FAILURE_V(AIMAPPER_ERROR_BAD_BUFFER, "%s: id=%" PRIu64,
                             "BAD_BUFFER(lockedUsage)", metadata.bufferID);
        }

        if ((accessRegion.left < 0) ||
                (accessRegion.top < 0) ||
                (accessRegion.bottom < accessRegion.top) ||
                (accessRegion.right < accessRegion.left) ||
                (accessRegion.right > metadata.width) ||
                (accessRegion.bottom > metadata.height)) {
            return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64,
                             "BAD_VALUE(accessRegion)", metadata.bufferID);
        }
        if (accessRegion.right && (accessRegion.left == accessRegion.right)) {
            return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64,
                             "BAD_VALUE(accessRegion)", metadata.bufferID);
        }
        if (accessRegion.bottom && (accessRegion.top == accessRegion.bottom)) {
            return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64,
                             "BAD_VALUE(accessRegion)", metadata.bufferID);
        }

        const uint8_t cpuUsage = uncheckedUsage & cb->usage & (kCPU_READ_MASK | kCPU_WRITE_MASK);
        if (cpuUsage == 0) {
            return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64,
                             "BAD_VALUE(uncheckedUsage)", metadata.bufferID);
        }
        if ((acquireFence >= 0) && waitFenceFd(acquireFence, __func__)) {
            return FAILURE_V(AIMAPPER_ERROR_NO_RESOURCES, "%s: id=%" PRIu64,
                             "NO_RESOURCES(acquireFence)", metadata.bufferID);
        }

        if (mDebugLevel >= DebugLevel::LOCK) {
            ALOGD("%s:%d: id=%" PRIu64 " usage=0x%X accessRegion="
                  "{ .left=%d, .top=%d, .right=%d, .bottom=%d }",
                  __func__, __LINE__, metadata.bufferID, cpuUsage, accessRegion.left,
                  accessRegion.top, accessRegion.right, accessRegion.bottom);
        }

        if (cb->hostHandle) {
            const AIMapper_Error e = readFromHost(*cb);
            if (e != AIMAPPER_ERROR_NONE) {
                return e;
            }
        }

        cb->lockedUsage = cpuUsage;
        *outData = cb->getBufferPtr();
        return AIMAPPER_ERROR_NONE;
    }

    AIMapper_Error unlock(const buffer_handle_t buffer, int* const releaseFence) const {
        cb_handle_t* const cb = validateCb(buffer);
        if (!cb) {
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }
        if (cb->lockedUsage == 0) {
            return FAILURE_V(AIMAPPER_ERROR_BAD_BUFFER, "%s: id=%" PRIu64,
                             "BAD_BUFFER(lockedUsage)", getID(*cb));
        }

        if (mDebugLevel >= DebugLevel::LOCK) {
            ALOGD("%s:%d: id=%" PRIu64, __func__, __LINE__, getID(*cb));
        }

        if (cb->hostHandle && (cb->lockedUsage & kCPU_WRITE_MASK)) {
            flushToHost(*cb);
        }

        cb->lockedUsage = 0;
        *releaseFence = -1;
        return AIMAPPER_ERROR_NONE;
    }

    AIMapper_Error flushLockedBuffer(const buffer_handle_t buffer) const {
        const cb_handle_t* const cb = validateCb(buffer);
        if (!cb) {
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }
        if (mDebugLevel >= DebugLevel::FLUSH) {
            ALOGD("%s:%d: id=%" PRIu64, __func__, __LINE__, getID(*cb));
        }
        if ((cb->lockedUsage & kCPU_WRITE_MASK) == 0) {
            return FAILURE_V(AIMAPPER_ERROR_BAD_BUFFER, "%s: id=%" PRIu64 ,
                             "BAD_BUFFER(lockedUsage)", getID(*cb));
        }
        if (cb->hostHandle) {
            flushToHost(*cb);
        }
        return AIMAPPER_ERROR_NONE;
    }

    AIMapper_Error rereadLockedBuffer(const buffer_handle_t buffer) const {
        const cb_handle_t* const cb = validateCb(buffer);
        if (!cb) {
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }
        if (mDebugLevel >= DebugLevel::FLUSH) {
            ALOGD("%s:%d: id=%" PRIu64, __func__, __LINE__, getID(*cb));
        }
        if ((cb->lockedUsage & kCPU_READ_MASK) == 0) {
            return FAILURE_V(AIMAPPER_ERROR_BAD_BUFFER, "%s: id=%" PRIu64 ,
                             "BAD_BUFFER(lockedUsage)", getID(*cb));
        }

        if (cb->hostHandle) {
            return readFromHost(*cb);
        } else {
            return AIMAPPER_ERROR_NONE;
        }
    }

    AIMapper_Error readFromHost(const cb_handle_t& cb) const {
        const CbExternalMetadata& metadata = getExternalMetadata(cb);
        const HostConnectionSession conn = getHostConnectionSession();
        ExtendedRCEncoderContext *const rcEnc = conn.getRcEncoder();

        const int res = rcEnc->rcColorBufferCacheFlush(
            rcEnc, cb.hostHandle, 0, true);
        if (res < 0) {
            return FAILURE_V(AIMAPPER_ERROR_NO_RESOURCES, "%s: id=%" PRIu64 " res=%d",
                             "NO_RESOURCES", metadata.bufferID, res);
        }

        if (isYuvFormat(getPixelFormat(cb))) {
            LOG_ALWAYS_FATAL_IF(!rcEnc->hasYUVCache());
            rcEnc->rcReadColorBufferYUV(rcEnc, cb.hostHandle,
                                        0, 0, metadata.width, metadata.height,
                                        cb.getBufferPtr(), cb.bufferSize);
        } else {
            LOG_ALWAYS_FATAL_IF(!rcEnc->featureInfo()->hasReadColorBufferDma);
            rcEnc->bindDmaDirectly(cb.getBufferPtr(),
                                   getMmapedPhysAddr(cb.getMmapedOffset()));
            rcEnc->rcReadColorBufferDMA(rcEnc, cb.hostHandle,
                                        0, 0, metadata.width, metadata.height,
                                        metadata.glFormat, metadata.glType,
                                        cb.getBufferPtr(), cb.bufferSize);
        }

        return AIMAPPER_ERROR_NONE;
    }

    void flushToHost(const cb_handle_t& cb) const {
        const CbExternalMetadata& metadata = getExternalMetadata(cb);
        const HostConnectionSession conn = getHostConnectionSession();
        ExtendedRCEncoderContext *const rcEnc = conn.getRcEncoder();

        rcEnc->bindDmaDirectly(cb.getBufferPtr(),
                               getMmapedPhysAddr(cb.getMmapedOffset()));
        rcEnc->rcUpdateColorBufferDMA(rcEnc, cb.hostHandle,
                                      0, 0, metadata.width, metadata.height,
                                      metadata.glFormat, metadata.glType,
                                      cb.getBufferPtr(), cb.bufferSize);
    }

    int32_t getMetadata(const buffer_handle_t buffer,
                        const AIMapper_MetadataType metadataType,
                        void* const destBuffer, const size_t destBufferSize) const {
        if (strcmp(metadataType.name, kStandardMetadataTypeStr)) {
            return -FAILURE_V(AIMAPPER_ERROR_UNSUPPORTED, "%s: name=%s",
                              "UNSUPPORTED", metadataType.name);
        } else {
            return getStandardMetadata(buffer, metadataType.value,
                                       destBuffer, destBufferSize);
        }
    }

    int32_t getStandardMetadata(const buffer_handle_t buffer,
                                const int64_t standardMetadataType,
                                void* const destBuffer,
                                const size_t destBufferSize) const {
        const cb_handle_t* const cb = validateCb(buffer);
        if (!cb) {
            return -FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }

        // don't log dry runs
        if (destBufferSize && (mDebugLevel >= DebugLevel::METADATA)) {
            ALOGD("%s:%d: id=%" PRIu64 " standardMetadataType=%" PRId64,
                  __func__, __LINE__, getID(*cb), standardMetadataType);
        }

        return getStandardMetadataImpl(*cb, MetadataWriter(destBuffer, destBufferSize),
                                       static_cast<StandardMetadataType>(standardMetadataType));
    }

    AIMapper_Error setMetadata(const buffer_handle_t buffer,
                               const AIMapper_MetadataType metadataType,
                               const void* const metadata, const size_t metadataSize) const {
        if (strcmp(metadataType.name, kStandardMetadataTypeStr)) {
            return FAILURE_V(AIMAPPER_ERROR_UNSUPPORTED, "%s: name=%s",
                             "UNSUPPORTED", metadataType.name);
        } else {
            return setStandardMetadata(buffer, metadataType.value,
                                       metadata, metadataSize);
        }
    }

    AIMapper_Error setStandardMetadata(const buffer_handle_t buffer,
                                       const int64_t standardMetadataType,
                                       const void* const metadata,
                                       const size_t metadataSize) const {
        const cb_handle_t* const cb = validateCb(buffer);
        if (!cb) {
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }

        if (mDebugLevel >= DebugLevel::METADATA) {
            ALOGD("%s:%d: id=%" PRIu64 " standardMetadataType=%" PRId64,
                  __func__, __LINE__, getID(*cb), standardMetadataType);
        }

        return setStandardMetadataImpl(*cb, MetadataReader(metadata, metadataSize),
                                       static_cast<StandardMetadataType>(standardMetadataType));
    }

    int32_t getStandardMetadataImpl(const cb_handle_t& cb, MetadataWriter writer,
                                    const StandardMetadataType standardMetadataType) const {
        const auto putMetadataHeader = [](MetadataWriter& writer,
                                          const StandardMetadataType standardMetadataType) -> MetadataWriter& {
            return writer.write(kStandardMetadataTypeTag)
                         .write(static_cast<int64_t>(standardMetadataType));
        };

        const CbExternalMetadata& metadata = getExternalMetadata(cb);
        switch (standardMetadataType) {
        case StandardMetadataType::BUFFER_ID:
            putMetadataHeader(writer, standardMetadataType)
                .write<uint64_t>(metadata.bufferID);
            break;

        case StandardMetadataType::NAME:
            putMetadataHeader(writer, standardMetadataType)
                .write(std::string_view(metadata.name, metadata.nameSize));
            break;

        case StandardMetadataType::WIDTH:
            putMetadataHeader(writer, standardMetadataType)
                .write<uint64_t>(metadata.width);
            break;

        case StandardMetadataType::HEIGHT:
            putMetadataHeader(writer, standardMetadataType)
                .write<uint64_t>(metadata.height);
            break;

        case StandardMetadataType::LAYER_COUNT:
            putMetadataHeader(writer, standardMetadataType)
                .write<uint64_t>(1);
            break;

        case StandardMetadataType::PIXEL_FORMAT_REQUESTED:
            putMetadataHeader(writer, standardMetadataType)
                .write<uint32_t>(cb.format);
            break;

        case StandardMetadataType::PIXEL_FORMAT_FOURCC:
            putMetadataHeader(writer, standardMetadataType)
                .write<uint32_t>(cb.drmformat);
            break;

        case StandardMetadataType::PIXEL_FORMAT_MODIFIER:
            putMetadataHeader(writer, standardMetadataType)
                .write<uint64_t>(DRM_FORMAT_MOD_LINEAR);
            break;

        case StandardMetadataType::USAGE:
            putMetadataHeader(writer, standardMetadataType)
                .write<uint64_t>(cb.usage);
            break;

        case StandardMetadataType::ALLOCATION_SIZE:
            putMetadataHeader(writer, standardMetadataType)
                .write<uint64_t>(cb.mmapedSize);
            break;

        case StandardMetadataType::PROTECTED_CONTENT:
            putMetadataHeader(writer, standardMetadataType)
                .write<uint64_t>((cb.usage & static_cast<uint64_t>(BufferUsage::PROTECTED))
                                 ? 1 : 0);
            break;

        case StandardMetadataType::COMPRESSION:
            putMetadataHeader(writer, standardMetadataType)
                .write(kCompressionTag)
                .write(static_cast<int64_t>(aahgc::Compression::NONE));
            break;

        case StandardMetadataType::INTERLACED:
            putMetadataHeader(writer, standardMetadataType)
                .write(kInterlacedTag)
                .write(static_cast<int64_t>(aahgc::Interlaced::NONE));
            break;

        case StandardMetadataType::CHROMA_SITING:
            putMetadataHeader(writer, standardMetadataType)
                .write(kChromaSitingTag)
                .write(static_cast<int64_t>(getFormatChromaSiting(getPixelFormat(cb))));
            break;

        case StandardMetadataType::PLANE_LAYOUTS: {
                const unsigned planeLayoutSize = metadata.planeLayoutSize;
                if (!planeLayoutSize) {
                    return -AIMAPPER_ERROR_UNSUPPORTED;
                }
                const PlaneLayoutComponent* const layoutComponents =
                    metadata.planeLayoutComponent;

                putMetadataHeader(writer, standardMetadataType)
                    .write<int64_t>(planeLayoutSize);
                for (unsigned plane = 0; plane < planeLayoutSize; ++plane) {
                    const auto& planeLayout = metadata.planeLayout[plane];
                    unsigned n = planeLayout.componentsSize;
                    const PlaneLayoutComponent* component =
                        layoutComponents + planeLayout.componentsBase;

                    writer.write<int64_t>(n);
                    for (; n > 0; --n, ++component) {
                        writer.write(kPlaneLayoutComponentTypeTag)
                              .write<int64_t>(component->type)
                              .write<int64_t>(component->offsetInBits)
                              .write<int64_t>(component->sizeInBits);
                    }

                    const unsigned horizontalSubsampling =
                        (1U << planeLayout.horizontalSubsamplingShift);
                    const unsigned verticalSubsampling =
                        (1U << planeLayout.verticalSubsamplingShift);

                    writer.write<int64_t>(planeLayout.offsetInBytes)
                          .write<int64_t>(planeLayout.sampleIncrementInBytes * CHAR_BIT)
                          .write<int64_t>(planeLayout.strideInBytes)
                          .write<int64_t>(metadata.width / horizontalSubsampling)
                          .write<int64_t>(metadata.height / verticalSubsampling)
                          .write<int64_t>(planeLayout.totalSizeInBytes)
                          .write<int64_t>(horizontalSubsampling)
                          .write<int64_t>(verticalSubsampling);
                }
            }
            break;

        case StandardMetadataType::CROP: {
                unsigned planeLayoutSize = metadata.planeLayoutSize;
                if (!planeLayoutSize) {
                    return -AIMAPPER_ERROR_UNSUPPORTED;
                }

                putMetadataHeader(writer, standardMetadataType)
                    .write<uint64_t>(planeLayoutSize);
                for (; planeLayoutSize > 0; --planeLayoutSize) {
                    /*
                     * b/359690632: `width`,`height` and `CROP` are uint64_t
                     * in the spec. But the metadata parser in Android uses
                     * int32_t for `CROP`.
                     */
                    writer.write<int32_t>(0).write<int32_t>(0)
                          .write<int32_t>(metadata.width)
                          .write<int32_t>(metadata.height);
                }
            }
            break;

        case StandardMetadataType::DATASPACE:
            putMetadataHeader(writer, standardMetadataType)
                .write<int32_t>(metadata.dataspace);
            break;

        case StandardMetadataType::BLEND_MODE:
            putMetadataHeader(writer, standardMetadataType)
                .write<int32_t>(metadata.blendMode);
            break;

        case StandardMetadataType::SMPTE2086:
            if (metadata.has_smpte2086) {
                const auto& smpte2086 = metadata.smpte2086;
                putMetadataHeader(writer, standardMetadataType)
                      .write(smpte2086.primaryRed.x).write(smpte2086.primaryRed.y)
                      .write(smpte2086.primaryGreen.x).write(smpte2086.primaryGreen.y)
                      .write(smpte2086.primaryBlue.x).write(smpte2086.primaryBlue.y)
                      .write(smpte2086.whitePoint.x).write(smpte2086.whitePoint.y)
                      .write(smpte2086.maxLuminance).write(smpte2086.minLuminance);
            }
            break;

        case StandardMetadataType::CTA861_3:
            if (metadata.has_cta861_3) {
                const auto& cta861_3 = metadata.cta861_3;
                putMetadataHeader(writer, standardMetadataType)
                      .write(cta861_3.maxContentLightLevel)
                      .write(cta861_3.maxFrameAverageLightLevel);
            }
            break;

        case StandardMetadataType::STRIDE: {
                const uint32_t value = (metadata.planeLayoutSize == 1) ?
                    (metadata.planeLayout[0].strideInBytes /
                     metadata.planeLayout[0].sampleIncrementInBytes) : 0;

                putMetadataHeader(writer, standardMetadataType).write(value);
            }
            break;

        default:
            return -FAILURE_V(AIMAPPER_ERROR_UNSUPPORTED,
                              "%s: id=%" PRIu64 ": unexpected standardMetadataType=%" PRId64,
                              "UNSUPPORTED", metadata.bufferID, static_cast<int64_t>(standardMetadataType));
        }

        return writer.desiredSize();
    }

    AIMapper_Error setStandardMetadataImpl(const cb_handle_t& cb, MetadataReader reader,
                                           const StandardMetadataType standardMetadataType) const {
        const auto checkMetadataHeader = [](MetadataReader& reader,
                                            const StandardMetadataType standardMetadataType) {
            if (reader.readString().compare(kStandardMetadataTypeTag)) {
                return false;
            }

            const std::optional<int64_t> type = reader.readInt<int64_t>();
            return type.has_value() &&
                   (type == static_cast<int64_t>(standardMetadataType)) &&
                   reader.ok();
        };

        CbExternalMetadata& metadata = getExternalMetadata(cb);
        switch (standardMetadataType) {
        case StandardMetadataType::DATASPACE:
            if (!checkMetadataHeader(reader, standardMetadataType)) {
                return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64 ": %s",
                                 "BAD_VALUE", metadata.bufferID, "DATASPACE");
            }

            reader.read(metadata.dataspace);
            if (!reader.ok()) {
                return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64 ": %s",
                                 "BAD_VALUE", metadata.bufferID, "DATASPACE");
            }
            break;

        case StandardMetadataType::BLEND_MODE:
            if (!checkMetadataHeader(reader, standardMetadataType)) {
                return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64 ": %s",
                                 "BAD_VALUE", metadata.bufferID, "BLEND_MODE");
            }
            reader.read(metadata.blendMode);
            if (!reader.ok()) {
                return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64 ": %s",
                                 "BAD_VALUE", metadata.bufferID, "BLEND_MODE");
            }
            break;

        case StandardMetadataType::SMPTE2086:
            if (reader.remaining() > 0) {
                if (!checkMetadataHeader(reader, standardMetadataType)) {
                    return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64 ": %s",
                                     "BAD_VALUE", metadata.bufferID, "SMPTE2086");
                }

                CbExternalMetadata::Smpte2086 smpte2086;
                reader.read(smpte2086.primaryRed.x).read(smpte2086.primaryRed.y)
                      .read(smpte2086.primaryGreen.x).read(smpte2086.primaryGreen.y)
                      .read(smpte2086.primaryBlue.x).read(smpte2086.primaryBlue.y)
                      .read(smpte2086.whitePoint.x).read(smpte2086.whitePoint.y)
                      .read(smpte2086.maxLuminance).read(smpte2086.minLuminance);
                if (reader.ok()) {
                    metadata.smpte2086 = smpte2086;
                    metadata.has_smpte2086 = true;
                } else {
                    return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64 ": %s",
                                     "BAD_VALUE", metadata.bufferID, "SMPTE2086");
                }
            } else {
                metadata.has_smpte2086 = false;
            }
            break;

        case StandardMetadataType::CTA861_3:
            if (reader.remaining() > 0) {
                if (!checkMetadataHeader(reader, standardMetadataType)) {
                    return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64 ": %s",
                                     "BAD_VALUE", metadata.bufferID, "CTA861_3");
                }

                CbExternalMetadata::Cta861_3 cta861_3;
                reader.read(cta861_3.maxContentLightLevel)
                      .read(cta861_3.maxFrameAverageLightLevel);
                if (reader.ok()) {
                    metadata.cta861_3 = cta861_3;
                    metadata.has_cta861_3 = true;
                } else {
                    return FAILURE_V(AIMAPPER_ERROR_BAD_VALUE, "%s: id=%" PRIu64 ": %s",
                                     "BAD_VALUE", metadata.bufferID, "CTA861_3");
                }
            } else {
                metadata.has_cta861_3 = false;
            }
            break;

        default:
            return FAILURE_V(AIMAPPER_ERROR_UNSUPPORTED,
                             "%s: id=%" PRIu64 ": standardMetadataType=%" PRId64,
                             "UNSUPPORTED", metadata.bufferID, static_cast<int64_t>(standardMetadataType));
        }

        return AIMAPPER_ERROR_NONE;
    }

    static AIMapper_Error listSupportedMetadataTypes(
            const AIMapper_MetadataTypeDescription** outDescriptionList,
            size_t* outNumberOfDescriptions) {
        *outDescriptionList = kMetadataTypeDescriptionList;
        *outNumberOfDescriptions = arraySize(kMetadataTypeDescriptionList);
        return AIMAPPER_ERROR_NONE;
    }

    AIMapper_Error dumpBuffer(const buffer_handle_t buffer,
                              const AIMapper_DumpBufferCallback dumpBufferCallback,
                              void* const context) const {
        const cb_handle_t* const cb = validateCb(buffer);
        if (!cb) {
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }

        if (mDebugLevel >= DebugLevel::METADATA) {
            ALOGD("%s:%d: id=%" PRIu64, __func__, __LINE__, getID(*cb));
        }

        std::vector<uint8_t> metadataBuffer(kMetadataBufferInitialSize);
        dumpBufferImpl(*cb, dumpBufferCallback, context, metadataBuffer);
        return AIMAPPER_ERROR_NONE;
    }

    void dumpBufferImpl(const cb_handle_t& cb,
                        const AIMapper_DumpBufferCallback dumpBufferCallback,
                        void* const context,
                        std::vector<uint8_t>& metadataBuffer) const {
        for (const auto& m : kMetadataTypeDescriptionList) {
            if (m.isGettable) {
                bool firstTry = true;
retryWithLargerBuffer:
                MetadataWriter writer(metadataBuffer.data(), metadataBuffer.size());
                const int32_t desiredSize = getStandardMetadataImpl(cb, writer,
                    static_cast<StandardMetadataType>(m.metadataType.value));
                if (desiredSize < 0) {
                    // should not happen, update `getStandardMetadata`
                    continue;
                } else if (desiredSize <= metadataBuffer.size()) {
                    (*dumpBufferCallback)(context, m.metadataType,
                                          metadataBuffer.data(), desiredSize);
                } else {
                    LOG_ALWAYS_FATAL_IF(!firstTry);
                    metadataBuffer.resize(desiredSize);
                    firstTry = false;
                    goto retryWithLargerBuffer;
                }
            }
        }
    }

    AIMapper_Error dumpAllBuffers(const AIMapper_BeginDumpBufferCallback beginDumpCallback,
                                  const AIMapper_DumpBufferCallback dumpBufferCallback,
                                  void* const context) const {
        std::vector<uint8_t> metadataBuffer(kMetadataBufferInitialSize);

        std::lock_guard<std::mutex> lock(mImportedBuffersMtx);
        for (const cb_handle_t* const cb : mImportedBuffers) {
            (*beginDumpCallback)(context);
            dumpBufferImpl(*cb, dumpBufferCallback, context, metadataBuffer);
        }

        return AIMAPPER_ERROR_NONE;
    }

    AIMapper_Error getReservedRegion(const buffer_handle_t buffer,
                                     void** const outReservedRegion,
                                     uint64_t* const outReservedSize) const {
        const cb_handle_t* const cb = validateCb(buffer);
        if (!cb) {
            return FAILURE(AIMAPPER_ERROR_BAD_BUFFER);
        }

        CbExternalMetadata& metadata = getExternalMetadata(*cb);
        const size_t reservedRegionSize = metadata.reservedRegionSize;
        if (reservedRegionSize) {
            *outReservedRegion = &metadata + 1;  // right after `CbExternalMetadata`
        } else {
            *outReservedRegion = nullptr;
        }
        *outReservedSize = reservedRegionSize;
        return AIMAPPER_ERROR_NONE;
    }

    cb_handle_t* validateCb(const buffer_handle_t buffer) const {
        cb_handle_t* cb = const_cast<cb_handle_t*>(static_cast<const cb_handle_t*>(buffer));
        std::lock_guard<std::mutex> lock(mImportedBuffersMtx);
        return mImportedBuffers.count(cb) ? cb : nullptr;
    }

    HostConnectionSession getHostConnectionSession() const {
        return HostConnectionSession(mHostConn.get());
    }

    uint64_t getMmapedPhysAddr(const uint64_t offset) const {
        return mPhysAddrToOffset + offset;
    }

    AIMapper mMapper;
    const std::unique_ptr<HostConnection> mHostConn;
    std::unordered_set<const cb_handle_t*> mImportedBuffers;
    uint64_t mPhysAddrToOffset;
    mutable std::mutex mImportedBuffersMtx;
    const DebugLevel mDebugLevel;
};
}  // namespace

extern "C" uint32_t ANDROID_HAL_MAPPER_VERSION = AIMAPPER_VERSION_5;

extern "C" AIMapper_Error AIMapper_loadIMapper(AIMapper* _Nullable* _Nonnull outImplementation) {
    static GoldfishMapper instance;
    *outImplementation = &instance.getAIMapper();
    return AIMAPPER_ERROR_NONE;
}
