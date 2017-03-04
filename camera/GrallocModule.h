
#ifndef EMU_CAMERA_GRALLOC_MODULE_
#define EMU_CAMERA_GRALLOC_MODULE_

#include <hardware/gralloc.h>

class GrallocModule
{
public:
  static GrallocModule &getInstance() {
    static GrallocModule instance;
    return instance;
  }

  int lock(buffer_handle_t handle,
      int usage, int l, int t, int w, int h, void **vaddr) {
    return mModule->lock(mModule, handle, usage, l, t, w, h, vaddr);
  }

  int lock_ycbcr(buffer_handle_t handle,
      int usage, int l, int t, int w, int h,
      struct android_ycbcr *ycbcr) {
    return mModule->lock_ycbcr(mModule, handle, usage, l, t, w, h, ycbcr);
  }

  int unlock(buffer_handle_t handle) {
    return mModule->unlock(mModule, handle);
  }

private:
  GrallocModule() {
    const hw_module_t *module = NULL;
    if (!hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module)) {
      ALOGE("%s: Failed to get gralloc module", __FUNCTION__);
    }
    mModule = reinterpret_cast<const gralloc_module_t*>(module);
  }
  const gralloc_module_t *mModule;
};

#endif
