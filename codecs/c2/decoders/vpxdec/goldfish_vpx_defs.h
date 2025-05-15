#ifndef MY_VPX_DEFS_H_
#define MY_VPX_DEFS_H_

#include <cstdint>

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

struct vpx_codec_ctx_t {
    vpx_image_t myImg;
    uint8_t *data;
    uint8_t *dst;
    uint64_t address_offset = 0;
    uint64_t id; // >= 1, unique

    uint32_t outputBufferWidth;
    uint32_t outputBufferHeight;
    uint32_t width;
    uint32_t height;

    int hostColorBufferId;
    int memory_slot;
    int version;        // 100: return decoded frame to guest; 200: render on host
    uint8_t vpversion;  // 8: vp8 or 9: vp9
    uint8_t bpp;
};

int vpx_codec_destroy(vpx_codec_ctx_t *);
int vpx_codec_dec_init(vpx_codec_ctx_t *);
vpx_image_t *vpx_codec_get_frame(vpx_codec_ctx_t *, int hostColorBufferId = -1);
int vpx_codec_flush(vpx_codec_ctx_t *ctx);
int vpx_codec_decode(vpx_codec_ctx_t *ctx, const uint8_t *data,
                     unsigned int data_sz, void *user_priv, long deadline);

void vpx_codec_send_metadata(vpx_codec_ctx_t *ctx, void*ptr);

#endif // MY_VPX_DEFS_H_
