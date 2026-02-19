/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <inttypes.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <log/log.h>
#include <gralloc_cb_bp.h>
#include <xf86drm.h>

#include <C2AllocatorGralloc.h>

#include "cros_gralloc_handle.h"
#include "virtgpu_drm.h"

namespace goldfish::media::c2 {

namespace {
using android::base::unique_fd;

bool isMinigbmFromProperty() {
  static constexpr const auto kGrallocProp = "ro.hardware.gralloc";

  const auto grallocProp = android::base::GetProperty(kGrallocProp, "");
  ALOGD("%s:codecs: minigbm query prop value is: %s", __FUNCTION__, grallocProp.c_str());

  if (grallocProp == "minigbm") {
    ALOGD("%s:codecs: Using minigbm, in minigbm mode.\n", __FUNCTION__);
    return true;
  } else {
    ALOGD("%s:codecs: Is not using minigbm, in goldfish mode.\n", __FUNCTION__);
    return false;
  }
}

class ColorBufferUtilsGlobalState {
public:
    ColorBufferUtilsGlobalState() : mIsMinigbm(isMinigbmFromProperty()) {
        if (mIsMinigbm) {
            static constexpr int kRendernodeMinor = 128;
            mRendernodeFd.reset(drmOpenRender(kRendernodeMinor));
        }
    }

    uint32_t getColorBufferHandle(native_handle_t const* handle) {
        if (mIsMinigbm) {
            struct drm_virtgpu_resource_info info;
            if (!getResInfo(handle, &info)) {
                ALOGE("%s: Error getting color buffer handle (minigbm case)", __func__);
                return -1;
            }
            return info.res_handle;
        } else {
            return static_cast<const cb_handle_t*>(handle)->hostHandle;
        }
    }

    uint64_t getColorBufferUsage(const native_handle_t* handle) {
        if (mIsMinigbm) {
            return static_cast<const cros_gralloc_handle*>(handle)->usage;
        } else {
            return static_cast<const cb_handle_t*>(handle)->usage;
        }
    }

    uint64_t getClientUsage(C2BlockPool& pool) {
        std::shared_ptr<C2GraphicBlock> myOutBlock;
        const C2MemoryUsage usage = {0, 0};
        const uint32_t format = HAL_PIXEL_FORMAT_YCBCR_420_888;
        pool.fetchGraphicBlock(2, 2, format, usage, &myOutBlock);
        if (!myOutBlock) {
            return 0;
        }

        auto c2Handle = myOutBlock->handle();
        return getColorBufferUsage(
                android::UnwrapNativeCodec2GrallocHandle(c2Handle));
    }

private:
    bool getResInfo(native_handle_t const* handle,
                    struct drm_virtgpu_resource_info* info) const {
        if (!mRendernodeFd.ok()) {
            ALOGE("%s: Error, rendernode fd missing\n", __func__);
            return false;
        }

        cros_gralloc_handle const* cros_handle = static_cast<cros_gralloc_handle const*>(handle);
        uint32_t prime_handle;
        int ret = drmPrimeFDToHandle(mRendernodeFd.get(), cros_handle->fds[0], &prime_handle);
        if (ret) {
            ALOGE("%s: DRM_IOCTL_PRIME_FD_TO_HANDLE failed: %s (errno %d)\n",
                  __func__, strerror(errno), errno);
            return false;
        }

        memset(info, 0x0, sizeof(*info));
        info->bo_handle = prime_handle;

        struct drm_gem_close gem_close;
        memset(&gem_close, 0x0, sizeof(gem_close));
        gem_close.handle = prime_handle;

        ret = drmIoctl(mRendernodeFd.get(), DRM_IOCTL_VIRTGPU_RESOURCE_INFO, info);
        if (ret) {
            ALOGE("%s: DRM_IOCTL_VIRTGPU_RESOURCE_INFO failed: %s (errno %d)\n",
                  __func__, strerror(errno), errno);
            drmIoctl(mRendernodeFd.get(), DRM_IOCTL_GEM_CLOSE, &gem_close);
            return false;
        }

        drmIoctl(mRendernodeFd.get(), DRM_IOCTL_GEM_CLOSE, &gem_close);
        return true;
    }

    unique_fd mRendernodeFd;
    const bool mIsMinigbm;
};

static ColorBufferUtilsGlobalState& getGlobals() {
    static ColorBufferUtilsGlobalState globals;
    return globals;
}

}  // namespace

uint32_t getColorBufferHandle(native_handle_t const* handle) {
    return getGlobals().getColorBufferHandle(handle);
}

uint64_t getClientUsage(C2BlockPool& pool) {
    return getGlobals().getClientUsage(pool);
}

}  // namespace goldfish::media::c2
