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

#include <cstring>
#include <string_view>

#include <sched.h>

#include <android-base/unique_fd.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <aidl/android/hardware/graphics/allocator/AllocationError.h>
#include <aidl/android/hardware/graphics/allocator/AllocationResult.h>
#include <aidl/android/hardware/graphics/allocator/BnAllocator.h>
#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/PixelFormat.h>
#include <aidl/android/hardware/graphics/common/PlaneLayoutComponentType.h>

#include <aidlcommonsupport/NativeHandle.h>

#include <debug.h>
#include <drm_fourcc.h>
#include <glUtils.h>
#include <goldfish_address_space.h>
#include <gralloc_cb_bp.h>
#include <qemu_pipe_bp.h>

#include "CbExternalMetadata.h"
#include "DebugLevel.h"
#include "HostConnectionSession.h"

using ::aidl::android::hardware::graphics::allocator::AllocationError;
using ::aidl::android::hardware::graphics::allocator::AllocationResult;
using ::aidl::android::hardware::graphics::allocator::BnAllocator;
using ::aidl::android::hardware::graphics::allocator::BufferDescriptorInfo;
using ::aidl::android::hardware::graphics::common::BufferUsage;
using ::aidl::android::hardware::graphics::common::PixelFormat;
using ::aidl::android::hardware::graphics::common::PlaneLayoutComponentType;

#ifndef GL_RGBA16F
#define GL_RGBA16F                        0x881A
#endif // GL_RGBA16F

#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT                     0x140B
#endif // GL_HALF_FLOAT

#ifndef GL_RGB10_A2
#define GL_RGB10_A2                       0x8059
#endif // GL_RGB10_A2

#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV    0x8368
#endif // GL_UNSIGNED_INT_2_10_10_10_REV

namespace {
enum class EmulatorFrameworkFormat : uint8_t {
    GL_COMPATIBLE = 0,
    YV12 = 1,
    YUV_420_888 = 2, // (Y+)(U+)(V+)
};

size_t align(const size_t value, const size_t alignmentP2) {
    return (value + alignmentP2 - 1) & ~(alignmentP2 - 1);
}

size_t strnlen(const char* str, const size_t maxSize) {
    const char* const begin = str;
    const char* const end = begin + maxSize;
    for (; *str && (str != end); ++str) {}
    return str - begin;
}

ndk::ScopedAStatus toBinderStatus(const AllocationError error) {
    return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(error));
}

uint64_t toUsage64(const BufferUsage usage) {
    return static_cast<uint64_t>(usage);
}

bool needGpuBuffer(const uint64_t usage) {
    return usage & (toUsage64(BufferUsage::GPU_TEXTURE)
                    | toUsage64(BufferUsage::GPU_RENDER_TARGET)
                    | toUsage64(BufferUsage::COMPOSER_OVERLAY)
                    | toUsage64(BufferUsage::COMPOSER_CLIENT_TARGET)
                    | toUsage64(BufferUsage::GPU_DATA_BUFFER));
}

bool needCpuBuffer(const uint64_t usage) {
    return usage & (toUsage64(BufferUsage::CPU_READ_MASK)
                    | toUsage64(BufferUsage::CPU_WRITE_MASK));
}

PlaneLayoutComponent makePlaneLayoutComponent(const PlaneLayoutComponentType type,
                                              const unsigned offsetInBits,
                                              const unsigned sizeInBits) {
    return {
        .type = static_cast<uint32_t>(type),
        .offsetInBits = static_cast<uint16_t>(offsetInBits),
        .sizeInBits = static_cast<uint16_t>(sizeInBits),
    };
}

size_t initPlaneLayout(PlaneLayout& plane,
                       const uint32_t width,
                       const uint32_t height,
                       const size_t offsetInBytes,
                       const uint32_t alignment,
                       const unsigned sampleSizeInBytes,
                       const unsigned subsamplingShift,
                       const unsigned componentsBase,
                       const unsigned componentsSize) {
    const uint32_t strideInBytes = align(width * sampleSizeInBytes, alignment);

    plane.offsetInBytes = offsetInBytes;
    plane.strideInBytes = strideInBytes;
    plane.totalSizeInBytes = strideInBytes * height;
    plane.sampleIncrementInBytes = sampleSizeInBytes;
    plane.horizontalSubsamplingShift = subsamplingShift;
    plane.verticalSubsamplingShift = subsamplingShift;
    plane.componentsBase = componentsBase;
    plane.componentsSize = componentsSize;

    return offsetInBytes + plane.totalSizeInBytes;
}

struct GoldfishAllocator : public BnAllocator {
    GoldfishAllocator()
        : mHostConn(HostConnection::createUnique(kCapsetNone))
        , mDebugLevel(getDebugLevel()) {}

    ndk::ScopedAStatus allocate2(const BufferDescriptorInfo& desc,
                                 const int32_t count,
                                 AllocationResult* const outResult) override {
        if (count <= 0) {
            return toBinderStatus(FAILURE_V(AllocationError::BAD_DESCRIPTOR,
                                            "%s: count=%d", "BAD_DESCRIPTOR",
                                            count));
        }
        if (desc.width <= 0) {
            return toBinderStatus(FAILURE_V(AllocationError::BAD_DESCRIPTOR,
                                            "%s: width=%d", "BAD_DESCRIPTOR",
                                            desc.width));
        }
        if (desc.height <= 0) {
            return toBinderStatus(FAILURE_V(AllocationError::BAD_DESCRIPTOR,
                                            "%s: height=%d", "BAD_DESCRIPTOR",
                                            desc.height));
        }
        if (!validateUsage(desc.usage)) {
            return toBinderStatus(FAILURE_V(AllocationError::BAD_DESCRIPTOR,
                                            "%s: usage=0x%" PRIX64, "BAD_DESCRIPTOR",
                                            toUsage64(desc.usage)));
        }
        if (desc.layerCount != 1) {
            return toBinderStatus(FAILURE_V(AllocationError::BAD_DESCRIPTOR,
                                            "%s: layerCount=%d", "BAD_DESCRIPTOR",
                                            desc.layerCount));
        }
        if (desc.reservedSize < 0) {
            return toBinderStatus(FAILURE_V(AllocationError::BAD_DESCRIPTOR,
                                            "%s: reservedSize=%" PRId64, "BAD_DESCRIPTOR",
                                            desc.reservedSize));
        }
        if (!desc.additionalOptions.empty()) {
            return toBinderStatus(FAILURE_V(
                AllocationError::BAD_DESCRIPTOR, "%s: %s", "BAD_DESCRIPTOR",
                "'BufferDescriptorInfo::additionalOptions' are not supported"));
        }

        const uint64_t usage = toUsage64(desc.usage);
        const uint32_t width = desc.width;
        const uint32_t height = desc.height;
        size_t offsetInBytes = 0;

        AllocationRequest req;
        switch (desc.format) {
        case PixelFormat::RGBA_8888:
            req.glFormat = GL_RGBA;
            req.glType = GL_UNSIGNED_BYTE;

            req.drmFormat = DRM_FORMAT_ABGR8888;

            req.planeSize = 1;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 4, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 4);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::R, 0, 8);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::G, 8, 8);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::B, 16, 8);
            req.planeComponent[3] = makePlaneLayoutComponent(PlaneLayoutComponentType::A, 24, 8);
            break;

        case PixelFormat::RGBX_8888:
            req.glFormat = GL_RGBA;
            req.glType = GL_UNSIGNED_BYTE;

            req.drmFormat = DRM_FORMAT_XBGR8888;

            req.planeSize = 1;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 4, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 3);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::R, 0, 8);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::G, 8, 8);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::B, 16, 8);
            break;

        case PixelFormat::BGRA_8888:
            req.glFormat = GL_RGBA;
            req.glType = GL_UNSIGNED_BYTE;

            req.drmFormat = DRM_FORMAT_ARGB8888;

            req.planeSize = 1;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 4, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 4);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::B, 0, 8);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::G, 8, 8);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::R, 16, 8);
            req.planeComponent[3] = makePlaneLayoutComponent(PlaneLayoutComponentType::A, 24, 8);
            break;

        case PixelFormat::RGB_888:
            if (needGpuBuffer(usage)) {
                return toBinderStatus(FAILURE(AllocationError::UNSUPPORTED));
            }

            req.drmFormat = DRM_FORMAT_BGR888;

            req.planeSize = 1;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 3, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 3);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::R, 0, 8);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::G, 8, 8);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::B, 16, 8);
            break;

        case PixelFormat::RGB_565:
            req.glFormat = GL_RGB565;
            req.glType = GL_UNSIGNED_SHORT_5_6_5;

            req.drmFormat = DRM_FORMAT_BGR565;

            req.planeSize = 1;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 2, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 3);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::R, 0, 5);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::G, 5, 6);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::B, 11, 5);
            break;

        case PixelFormat::RGBA_FP16:
            req.glFormat = GL_RGBA16F;
            req.glType = GL_HALF_FLOAT;

            req.drmFormat = DRM_FORMAT_ABGR16161616F;

            req.planeSize = 1;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 8, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 4);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::R, 0, 16);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::G, 16, 16);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::B, 32, 16);
            req.planeComponent[3] = makePlaneLayoutComponent(PlaneLayoutComponentType::A, 48, 16);
            break;

        case PixelFormat::RGBA_1010102:
            req.glFormat = GL_RGB10_A2;
            req.glType = GL_UNSIGNED_INT_2_10_10_10_REV;

            req.drmFormat = DRM_FORMAT_ABGR2101010;

            req.planeSize = 1;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 4, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 4);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::R, 0, 10);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::G, 10, 10);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::B, 20, 10);
            req.planeComponent[3] = makePlaneLayoutComponent(PlaneLayoutComponentType::A, 30, 2);
            break;

        case PixelFormat::RAW16:
            if (needGpuBuffer(usage)) {
                return toBinderStatus(FAILURE(AllocationError::UNSUPPORTED));
            }

            req.drmFormat = DRM_FORMAT_R16;

            req.planeSize = 1;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 16,
                /*sampleSizeInBytes=*/ 2, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 1);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::RAW, 0, 16);
            break;

        case PixelFormat::Y16:
            if (needGpuBuffer(usage)) {
                return toBinderStatus(FAILURE(AllocationError::UNSUPPORTED));
            }

            req.drmFormat = DRM_FORMAT_R16;

            req.planeSize = 1;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 16,
                /*sampleSizeInBytes=*/ 2, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 1);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::Y, 0, 16);
            break;

        case PixelFormat::BLOB:
            if (needGpuBuffer(usage)) {
                return toBinderStatus(FAILURE(AllocationError::UNSUPPORTED));
            }

            req.planeSize = 1;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 1, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 1);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::RAW, 0, 8);
            break;

        case PixelFormat::YCRCB_420_SP:  // Y + CrCb interleaved
            if (needGpuBuffer(usage)) {
                return toBinderStatus(FAILURE(AllocationError::UNSUPPORTED));
            }

            req.drmFormat = DRM_FORMAT_YVU420;

            req.planeSize = 2;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 1, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 1);
            offsetInBytes = initPlaneLayout(
                req.plane[1], width / 2, height / 2, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 2, /*subsamplingShift=*/ 1,
                /*componentsBase=*/ 1, /*componentsSize*/ 2);
            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::Y, 0, 8);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::CR, 0, 8);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::CB, 8, 8);
            break;

        case PixelFormat::YV12:  // 3 planes (Y, Cr, Cb), 16bytes aligned
            req.glFormat = GL_RGBA;
            req.glType = GL_UNSIGNED_BYTE;
            req.emuFwkFormat = EmulatorFrameworkFormat::YV12;

            req.drmFormat = DRM_FORMAT_YVU420;

            req.planeSize = 3;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 16,
                /*sampleSizeInBytes=*/ 1, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 1);
            offsetInBytes = initPlaneLayout(
                req.plane[1], width / 2, height / 2, offsetInBytes, /*alignment=*/ 16,
                /*sampleSizeInBytes=*/ 1, /*subsamplingShift=*/ 1,
                /*componentsBase=*/ 1, /*componentsSize*/ 1);
            offsetInBytes = initPlaneLayout(
                req.plane[2], width / 2, height / 2, offsetInBytes, /*alignment=*/ 16,
                /*sampleSizeInBytes=*/ 1, /*subsamplingShift=*/ 1,
                /*componentsBase=*/ 2, /*componentsSize*/ 1);

            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::Y, 0, 8);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::CR, 0, 8);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::CB, 0, 8);
            break;

        case PixelFormat::YCBCR_420_888:  // 3 planes (Y, Cb, Cr)
            req.glFormat = GL_RGBA;
            req.glType = GL_UNSIGNED_BYTE;
            req.emuFwkFormat = EmulatorFrameworkFormat::YUV_420_888;

            req.drmFormat = DRM_FORMAT_YUV420;

            req.planeSize = 3;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 1, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 1);
            offsetInBytes = initPlaneLayout(
                req.plane[1], width / 2, height / 2, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 1, /*subsamplingShift=*/ 1,
                /*componentsBase=*/ 1, /*componentsSize*/ 1);
            offsetInBytes = initPlaneLayout(
                req.plane[2], width / 2, height / 2, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 1, /*subsamplingShift=*/ 1,
                /*componentsBase=*/ 2, /*componentsSize*/ 1);

            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::Y, 0, 8);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::CB, 0, 8);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::CR, 0, 8);
            break;

        case PixelFormat::YCBCR_P010:  // Y + CbCr interleaved, 2bytes per component
            req.glFormat = GL_RGBA;
            req.glType = GL_UNSIGNED_BYTE;

            req.drmFormat = DRM_FORMAT_YUV420_10BIT;

            req.planeSize = 2;
            offsetInBytes = initPlaneLayout(
                req.plane[0], width, height, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 2, /*subsamplingShift=*/ 0,
                /*componentsBase=*/ 0, /*componentsSize*/ 1);
            offsetInBytes = initPlaneLayout(
                req.plane[1], width / 2, height / 2, offsetInBytes, /*alignment=*/ 1,
                /*sampleSizeInBytes=*/ 4, /*subsamplingShift=*/ 1,
                /*componentsBase=*/ 1, /*componentsSize*/ 2);

            req.planeComponent[0] = makePlaneLayoutComponent(PlaneLayoutComponentType::Y, 6, 10);
            req.planeComponent[1] = makePlaneLayoutComponent(PlaneLayoutComponentType::CB, 6, 10);
            req.planeComponent[2] = makePlaneLayoutComponent(PlaneLayoutComponentType::CR, 6 + 10 + 6, 10);
            break;

        default:
            return toBinderStatus(FAILURE_V(AllocationError::UNSUPPORTED,
                                            "Unsupported format: format=0x%X, usage=%" PRIX64,
                                            static_cast<uint32_t>(desc.format), desc.usage));
        }

        req.name = std::string_view(reinterpret_cast<const char*>(desc.name.data()),
                                    strnlen(reinterpret_cast<const char*>(desc.name.data()),
                                    desc.name.size()));
        req.usage = usage;
        req.width = width;
        req.height = height;
        req.format = desc.format;
        req.reservedRegionSize = desc.reservedSize;

        if (needCpuBuffer(usage)) {
            req.imageSizeInBytes = offsetInBytes;
            req.stride0 = (req.planeSize == 1) ?
                              (req.plane[0].strideInBytes /
                               req.plane[0].sampleIncrementInBytes) : 0;
        } else {
            req.imageSizeInBytes = 0;   // the image is not allocated
            /*
             * b/359874912: the spec does not say how to handle PLANE_LAYOUTS
             * if the CPU buffer is not allocated. Let's not populate them
             * without the CPU buffer (sizes and offsets don't make sense anyway).
             */
            req.planeSize = 0;
            req.stride0 = 0;
        }

        if (needGpuBuffer(usage)) {
            req.rcAllocFormat = (req.format == PixelFormat::RGBX_8888) ? GL_RGB : req.glFormat;
        } else {
            req.glFormat = -1;  // no GPU buffer - no GPU formats
            req.glType = -1;
            req.rcAllocFormat = -1;
        }

        std::vector<std::unique_ptr<cb_handle_t>> cbs(count);

        {
            HostConnectionSession connSession(mHostConn.get());
            ExtendedRCEncoderContext* const rcEnc = connSession.getRcEncoder();
            LOG_ALWAYS_FATAL_IF(!rcEnc);
            const bool hasSharedSlots =
                rcEnc->featureInfo_const()->hasSharedSlotsHostMemoryAllocator;

            for (int i = 0; i < count; ++i) {
                std::unique_ptr<cb_handle_t> cb = allocateImpl(
                    req, *rcEnc, ++mBufferIdGenerator, hasSharedSlots);
                if (cb) {
                    cbs[i] = std::move(cb);
                } else {
                    for (--i; i > 0; --i) {
                        unallocate(std::move(cbs[i]));
                    }
                    return toBinderStatus(FAILURE(AllocationError::NO_RESOURCES));
                }
            }
        }

        outResult->stride = req.stride0;
        outResult->buffers.reserve(count);
        for (auto& cb : cbs) {
            outResult->buffers.push_back(android::dupToAidl(cb.get()));
            unallocate(std::move(cb));
        }

        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus isSupported(const BufferDescriptorInfo& descriptor,
                                   bool* outResult) override {
        *outResult = isSupportedImpl(descriptor);
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus getIMapperLibrarySuffix(std::string* outResult) override {
        *outResult = "ranchu";
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus allocate(const std::vector<uint8_t>& encodedDescriptor,
                                const int32_t count,
                                AllocationResult* const outResult) override {
        (void)encodedDescriptor;
        (void)count;
        (void)outResult;
        return toBinderStatus(FAILURE(AllocationError::UNSUPPORTED));
    }

private:
    struct AllocationRequest {
        std::string_view name;
        PlaneLayout plane[3];
        PlaneLayoutComponent planeComponent[4];
        size_t imageSizeInBytes = 0;
        size_t reservedRegionSize = 0;
        uint64_t usage = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride0 = 0;
        uint32_t drmFormat = DRM_FORMAT_INVALID;
        PixelFormat format = PixelFormat::UNSPECIFIED;
        int glFormat = -1;
        int glType = -1;
        int rcAllocFormat = -1;
        EmulatorFrameworkFormat emuFwkFormat = EmulatorFrameworkFormat::GL_COMPATIBLE;
        uint8_t planeSize = 0;
    };

    std::unique_ptr<cb_handle_t>
    allocateImpl(const AllocationRequest& req,
                 ExtendedRCEncoderContext& rcEnc,
                 const uint64_t bufferID,
                 const bool hasSharedSlots) const {
        android::base::unique_fd cpuAlocatorFd;
        GoldfishAddressSpaceBlock bufferBits;
        const size_t imageSizeInBytesAligned = align(req.imageSizeInBytes, 16);
        const size_t totalAllocationSize =
            imageSizeInBytesAligned + sizeof(CbExternalMetadata) + req.reservedRegionSize;

        {
            GoldfishAddressSpaceHostMemoryAllocator hostMemoryAllocator(hasSharedSlots);
            LOG_ALWAYS_FATAL_IF(!hostMemoryAllocator.is_opened());

            if (hostMemoryAllocator.hostMalloc(&bufferBits, totalAllocationSize)) {
                return FAILURE(nullptr);
            }

            cpuAlocatorFd.reset(hostMemoryAllocator.release());

            CbExternalMetadata& metadata =
                *reinterpret_cast<CbExternalMetadata*>(
                    static_cast<char*>(bufferBits.guestPtr()) + imageSizeInBytesAligned);

            memset(&metadata, 0, sizeof(metadata));
            metadata.magic = CbExternalMetadata::kMagicValue;
            metadata.bufferID = bufferID;
            metadata.nameSize = std::min(req.name.size(), sizeof(CbExternalMetadata::name));
            memcpy(metadata.name, req.name.data(), metadata.nameSize);

            metadata.planeLayoutSize = req.planeSize;
            if (req.planeSize) {
                static_assert(sizeof(metadata.planeLayout) == sizeof(req.plane));
                memcpy(metadata.planeLayout, req.plane, sizeof(req.plane));

                static_assert(sizeof(metadata.planeLayoutComponent) ==
                              sizeof(req.planeComponent));
                memcpy(metadata.planeLayoutComponent, req.planeComponent,
                       sizeof(req.planeComponent));
            }

            metadata.reservedRegionSize = req.reservedRegionSize;
            metadata.width = req.width;
            metadata.height = req.height;
            metadata.glFormat = req.glFormat;
            metadata.glType = req.glType;
        }

        uint32_t hostHandle = 0;
        android::base::unique_fd hostHandleRefCountFd;
        if (needGpuBuffer(req.usage)) {
            hostHandleRefCountFd.reset(qemu_pipe_open("refcount"));
            if (!hostHandleRefCountFd.ok()) {
                return FAILURE(nullptr);
            }

            hostHandle = rcEnc.rcCreateColorBufferDMA(
                &rcEnc, req.width, req.height,
                req.rcAllocFormat, static_cast<int>(req.emuFwkFormat));
            if (!hostHandle) {
                return FAILURE(nullptr);
            }

            if (qemu_pipe_write(hostHandleRefCountFd.get(),
                                &hostHandle,
                                sizeof(hostHandle)) != sizeof(hostHandle)) {
                rcEnc.rcCloseColorBuffer(&rcEnc, hostHandle);
                return FAILURE(nullptr);
            }
        }

        if (mDebugLevel >= DebugLevel::ALLOC) {
            char hostHandleValueStr[128];
            if (hostHandle) {
                snprintf(hostHandleValueStr, sizeof(hostHandleValueStr),
                         "0x%X glFormat=0x%X glType=0x%X "
                         "rcAllocFormat=0x%X emuFwkFormat=%d",
                         hostHandle, req.glFormat, req.glType, req.rcAllocFormat,
                         static_cast<int>(req.emuFwkFormat));
            } else {
                strcpy(hostHandleValueStr, "null");
            }

            char bufferValueStr[96];
            if (req.imageSizeInBytes) {
                snprintf(bufferValueStr, sizeof(bufferValueStr),
                         "{ ptr=%p mappedSize=%zu offset=0x%" PRIX64 " } imageSizeInBytes=%zu",
                         bufferBits.guestPtr(), size_t(bufferBits.size()),
                         bufferBits.offset(), size_t(req.imageSizeInBytes));
            } else {
                strcpy(bufferValueStr, "null");
            }

            ALOGD("%s:%d name='%.*s' id=%" PRIu64 " width=%u height=%u format=0x%X "
                  "usage=0x%" PRIX64 " hostHandle=%s buffer=%s reservedSize=%zu",
                  __func__, __LINE__, int(req.name.size()), req.name.data(), bufferID,
                  req.width, req.height, static_cast<uint32_t>(req.format),
                  req.usage, hostHandleValueStr, bufferValueStr,
                  req.reservedRegionSize);
        }

        auto cb = std::make_unique<cb_handle_t>(
            cpuAlocatorFd.release(), hostHandleRefCountFd.release(), hostHandle,
            req.usage, static_cast<uint32_t>(req.format), req.drmFormat,
            req.stride0, req.imageSizeInBytes, bufferBits.guestPtr(),
            bufferBits.size(), bufferBits.offset(),
            imageSizeInBytesAligned);

        bufferBits.release();  // now cb owns it
        return cb;
    }

    static void unallocate(const std::unique_ptr<cb_handle_t> cb) {
        if (cb->hostHandleRefcountFd >= 0) {
            ::close(cb->hostHandleRefcountFd);
        }

        if (cb->bufferFd >= 0) {
            if (cb->mmapedSize > 0) {
                GoldfishAddressSpaceBlock::memoryUnmap(cb->getBufferPtr(), cb->mmapedSize);
            }

            GoldfishAddressSpaceHostMemoryAllocator::closeHandle(cb->bufferFd);
        }
    }

    static bool validateUsage(const BufferUsage usage) {
        static constexpr uint64_t kReservedUsage =
            (1U << 10) | (1U << 13) | (1U << 19) | (1U << 21);

        return 0 == (toUsage64(usage) & kReservedUsage);
    }

    static bool isSupportedImpl(const BufferDescriptorInfo& desc) {
        if (desc.width <= 0) { return false; }
        if (desc.height <= 0) { return false; }
        if (desc.layerCount != 1) { return false; }
        if (desc.reservedSize < 0) { return false; }
        if (!desc.additionalOptions.empty()) { return false; }

        switch (desc.format) {
        case PixelFormat::RGBA_8888:
        case PixelFormat::RGBX_8888:
        case PixelFormat::BGRA_8888:
        case PixelFormat::RGB_565:
        case PixelFormat::RGBA_FP16:
        case PixelFormat::RGBA_1010102:
        case PixelFormat::YV12:
        case PixelFormat::YCBCR_420_888:
        case PixelFormat::YCBCR_P010:
            return validateUsage(desc.usage);

        case PixelFormat::RGB_888:
        case PixelFormat::YCRCB_420_SP:
        case PixelFormat::RAW16:
        case PixelFormat::Y16:
        case PixelFormat::BLOB:
            return validateUsage(desc.usage) &&
                   !needGpuBuffer(toUsage64(desc.usage));

        case PixelFormat::IMPLEMENTATION_DEFINED:  // we don't support it
        default:
            return false;
        }
    }

    const std::unique_ptr<HostConnection> mHostConn;
    uint64_t mBufferIdGenerator = 0;
    const DebugLevel mDebugLevel;
};
}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    struct sched_param param = {0};
    param.sched_priority = 2;
    if (sched_setscheduler(0, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
        ALOGW("Failed to set priority: %s", strerror(errno));
    }

    auto allocator = ndk::SharedRefBase::make<GoldfishAllocator>();

    {
        const std::string instance = std::string(GoldfishAllocator::descriptor) + "/default";
        if (AServiceManager_addService(allocator->asBinder().get(),
                                       instance.c_str()) != STATUS_OK) {
            ALOGE("Failed to register: '%s'", instance.c_str());
            return EXIT_FAILURE;
        }
    }

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;    // joinThreadPool is not expected to return
}
