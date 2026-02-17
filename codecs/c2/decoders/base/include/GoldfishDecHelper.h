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

#include <log/log.h>
#include <malloc.h>
#include <inttypes.h>

/*
 * DO NOT #include "iv.h" and "ivd.h" files here. They are codec specific
 * and potentially contain different definitions of the types referred here.
 */

namespace goldfish::media::c2 {

template <typename Traits>
class GoldfishDecHelper {
  public:
    GoldfishDecHelper(int w, int h) : mWidth(w), mHeight(h), mStride(w) {
        Traits::createDecoder(mDecHandle, mIvColorformat, mNumCores, mStride);
    }
    ~GoldfishDecHelper() {
        if (mDecHandle) {
            Traits::destroyDecoder(mDecHandle);
        }
    }

    static bool isKeyFrame(const uint8_t *frame, int inSize) {
        return Traits::isKeyFrame(frame, inSize);
    }

    bool decodeHeader(const uint8_t *frame, int inSize, bool &status) {
        status = true;
        if (!Traits::isKeyFrame(frame, inSize)) {
            return false;
        }

        typename Traits::DecodeIp decodeIp = {};
        typename Traits::DecodeOp decodeOp = {};

        ivd_video_decode_ip_t *ps_decode_ip = &decodeIp.s_ivd_video_decode_ip_t;
        ivd_video_decode_op_t *ps_decode_op = &decodeOp.s_ivd_video_decode_op_t;

        setDecodeArgs(ps_decode_ip, ps_decode_op, frame, mStride, 0, inSize, 0);

        Traits::setParams(mDecHandle, mStride, IVD_DECODE_HEADER);

        IV_API_CALL_STATUS_T ret = Traits::callApi(mDecHandle, ps_decode_ip, ps_decode_op);

        if (IVD_RES_CHANGED == (ps_decode_op->u4_error_code & IVD_ERROR_MASK)) {
            Traits::resetDecoder(mDecHandle);
            Traits::setNumCores(mDecHandle, mNumCores);
            Traits::setParams(mDecHandle, mStride, IVD_DECODE_HEADER);
            ret = Traits::callApi(mDecHandle, ps_decode_ip, ps_decode_op);
        }

        if (ret != IV_SUCCESS) {
            if (Traits::shouldIgnoreApiError()) {
                ALOGW("decoder function returned error but continuing for this codec");
            } else {
                ALOGE("failed to call decoder function for header");
                status = false;
                return false;
            }
        }

        if (0 < ps_decode_op->u4_pic_wd && 0 < ps_decode_op->u4_pic_ht) {
            if (ps_decode_op->u4_pic_wd != mWidth || ps_decode_op->u4_pic_ht != mHeight) {
                mWidth = ps_decode_op->u4_pic_wd;
                mHeight = ps_decode_op->u4_pic_ht;
                return true;
            }
        }

        if (ps_decode_op->i4_reorder_depth >= 0) {
            if (mOutputDelay != ps_decode_op->i4_reorder_depth) {
                mOutputDelay = ps_decode_op->i4_reorder_depth;
            }
        }

        return false;
    }

    int getWidth() const { return mWidth; }
    int getHeight() const { return mHeight; }

  private:
    bool setDecodeArgs(ivd_video_decode_ip_t *ps_decode_ip,
                       ivd_video_decode_op_t *ps_decode_op,
                       const uint8_t *inBuffer, uint32_t displayStride,
                       size_t inOffset, size_t inSize, uint32_t tsMarker) {
        size_t lumaSize = displayStride * mHeight;
        size_t chromaSize = lumaSize >> 2;

        if (mStride != displayStride) {
            mStride = displayStride;
        }

        Traits::setParams(mDecHandle, mStride, IVD_DECODE_HEADER);

        ps_decode_ip->u4_size = sizeof(typename Traits::DecodeIp);
        ps_decode_ip->e_cmd = IVD_CMD_VIDEO_DECODE;
        if (inBuffer) {
            ps_decode_ip->u4_ts = tsMarker;
            ps_decode_ip->pv_stream_buffer = const_cast<uint8_t *>(inBuffer) + inOffset;
            ps_decode_ip->u4_num_Bytes = inSize;
        } else {
            ps_decode_ip->u4_ts = 0;
            ps_decode_ip->pv_stream_buffer = nullptr;
            ps_decode_ip->u4_num_Bytes = 0;
        }

        ps_decode_ip->s_out_buffer.u4_min_out_buf_size[0] = lumaSize;
        ps_decode_ip->s_out_buffer.u4_min_out_buf_size[1] = chromaSize;
        ps_decode_ip->s_out_buffer.u4_min_out_buf_size[2] = chromaSize;
        ps_decode_ip->s_out_buffer.pu1_bufs[0] = nullptr;
        ps_decode_ip->s_out_buffer.pu1_bufs[1] = nullptr;
        ps_decode_ip->s_out_buffer.pu1_bufs[2] = nullptr;
        ps_decode_ip->s_out_buffer.u4_num_bufs = 3;

        ps_decode_op->u4_size = sizeof(typename Traits::DecodeOp);
        ps_decode_op->u4_output_present = 0;

        return true;
    }

    iv_obj_t *mDecHandle = nullptr;
    int mWidth = 320;
    int mHeight = 240;
    int mNumCores = 1;
    int mStride = 16;
    int mOutputDelay = 8;
    IV_COLOR_FORMAT_T mIvColorformat = IV_YUV_420P;
};

} // namespace goldfish::media::c2
