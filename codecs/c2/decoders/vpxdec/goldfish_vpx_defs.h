#pragma once

#include <cstdint>

#include "goldfish_media_utils.h"

#define VPX_IMG_FMT_PLANAR 0x100       /**< Image is a planar format. */
#define VPX_IMG_FMT_UV_FLIP 0x200      /**< V plane precedes U in memory. */
#define VPX_IMG_FMT_HAS_ALPHA 0x400    /**< Image has an alpha channel. */
#define VPX_IMG_FMT_HIGHBITDEPTH 0x800 /**< Image uses 16bit framebuffer. */

typedef int vpx_codec_err_t;

enum class RenderMode {
    RENDER_BY_HOST_GPU = 1,
    RENDER_BY_GUEST_CPU = 2,
};

enum vpx_img_fmt_t {
    VPX_IMG_FMT_NONE,
    VPX_IMG_FMT_YV12 =
        VPX_IMG_FMT_PLANAR | VPX_IMG_FMT_UV_FLIP | 1, /**< planar YVU */
    VPX_IMG_FMT_I420 = VPX_IMG_FMT_PLANAR | 2,
    VPX_IMG_FMT_I422 = VPX_IMG_FMT_PLANAR | 5,
    VPX_IMG_FMT_I444 = VPX_IMG_FMT_PLANAR | 6,
    VPX_IMG_FMT_I440 = VPX_IMG_FMT_PLANAR | 7,
    VPX_IMG_FMT_I42016 = VPX_IMG_FMT_I420 | VPX_IMG_FMT_HIGHBITDEPTH,
    VPX_IMG_FMT_I42216 = VPX_IMG_FMT_I422 | VPX_IMG_FMT_HIGHBITDEPTH,
    VPX_IMG_FMT_I44416 = VPX_IMG_FMT_I444 | VPX_IMG_FMT_HIGHBITDEPTH,
    VPX_IMG_FMT_I44016 = VPX_IMG_FMT_I440 | VPX_IMG_FMT_HIGHBITDEPTH
};

struct vpx_image_t {
    void *user_priv;
    uint32_t d_w;       /**< Displayed image width */
    uint32_t d_h;       /**< Displayed image height */
    vpx_img_fmt_t fmt;  /**< Image Format */
};

#define VPX_CODEC_OK 0

class VpxCodecCtx {
public:
    VpxCodecCtx(uint8_t mVpVersion, int version);
    ~VpxCodecCtx();

    int init();
    void setupParameters(uint32_t width, uint32_t height,
                         int hostColorBufferId,
                         uint32_t outputBufferWidth, uint32_t mOutputBufferHeight,
                         uint8_t bpp);

    const vpx_image_t* getFrame(int hostColorBufferId = -1);
    const uint8_t* getDst() const;
    void sendMetadata(const MetaDataColorAspects& meta) const;
    int decode(const uint8_t *data, size_t dataSz,
               void *userPriv, long deadline);
    int flush();

private:
    void sendOperation(MediaOperation) const;

    vpx_image_t mImg;
    uint8_t *mData = nullptr;
    uint8_t *mDst = nullptr;
    uint64_t mAddressOffset = 0;
    uint64_t mId = 0;           // >= 1, unique

    uint32_t mOutputBufferWidth = 0;
    uint32_t mOutputBufferHeight = 0;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;

    int mHostColorBufferId = -1;
    int mMemorySlot = -1;
    int mVersion = 0;           // 100: return decoded frame to guest; 200: render on host
    const uint8_t mVpVersion;   // 8: vp8 or 9: vp9
    uint8_t mBpp = 0;
};