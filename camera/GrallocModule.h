#ifndef EMU_CAMERA_GRALLOC_MODULE_H
#define EMU_CAMERA_GRALLOC_MODULE_H

#include "gralloc_cb.h"
#include <assert.h>
#include <hardware/gralloc.h>
#include <log/log.h>

class GrallocModule
{
public:
  static GrallocModule &getInstance() {
    static GrallocModule instance;
    return instance;
  }

  ~GrallocModule() {
      gralloc_close(mAllocDev);
  }

  int lock(buffer_handle_t handle,
      int usage, int l, int t, int w, int h, void **vaddr) {
    return mModule->lock(mModule, handle, usage, l, t, w, h, vaddr);
  }

#ifdef GRALLOC_MODULE_API_VERSION_0_2
  int lock_ycbcr(buffer_handle_t handle,
      int usage, int l, int t, int w, int h,
      struct android_ycbcr *ycbcr) {
    return mModule->lock_ycbcr(mModule, handle, usage, l, t, w, h, ycbcr);
  }
#endif

  int unlock(buffer_handle_t handle) {
    return mModule->unlock(mModule, handle);
  }

  int alloc(int w, int h, int format, int usage, buffer_handle_t* handle) {
      int stride;
      return mAllocDev->alloc(mAllocDev, w, h, format, usage, handle, &stride);
  }

  int free(buffer_handle_t handle) {
      return mAllocDev->free(mAllocDev, handle);
  }

  uint64_t getOffset(const buffer_handle_t handle) {
      const cb_handle_t* cb_handle = cb_handle_t::from(handle);
      return cb_handle->getMmapedOffset();
  }

private:
  GrallocModule() {
    const hw_module_t *module = NULL;
    int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
    assert(ret == 0 && "Failed to get gralloc module");
    mModule = reinterpret_cast<const gralloc_module_t*>(module);
    ret = gralloc_open(module, &mAllocDev);
    assert(ret == 0 && "Fail to open GPU device");
  }

  const gralloc_module_t *mModule;
  alloc_device_t* mAllocDev;

};

#endif
