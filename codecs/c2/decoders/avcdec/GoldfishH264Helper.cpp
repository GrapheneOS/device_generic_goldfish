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

#define LOG_TAG "GoldfishH264Helper"

#include "GoldfishH264Helper.h"

#include <malloc.h>
#include <log/log.h>
#include <Codec2Mapper.h>

#define ivdec_api_function              ih264d_api_function
#define ivdext_create_ip_t              ih264d_create_ip_t
#define ivdext_create_op_t              ih264d_create_op_t
#define ivdext_delete_ip_t              ih264d_delete_ip_t
#define ivdext_delete_op_t              ih264d_delete_op_t
#define ivdext_ctl_set_num_cores_ip_t   ih264d_ctl_set_num_cores_ip_t
#define ivdext_ctl_set_num_cores_op_t   ih264d_ctl_set_num_cores_op_t
#define ALIGN128(x)                     ((((x) + 127) >> 7) << 7)
#define IVDEXT_CMD_CTL_SET_NUM_CORES    \
        (IVD_CONTROL_API_COMMAND_TYPE_T)IH264D_CMD_CTL_SET_NUM_CORES

namespace android {

static void *ivd_aligned_malloc(void *ctxt, WORD32 alignment, WORD32 size) {
    (void) ctxt;
    return memalign(alignment, size);
}

static void ivd_aligned_free(void *ctxt, void *mem) {
    (void) ctxt;
    free(mem);
}

void H264Traits::createDecoder(iv_obj_t *&decHandle, IV_COLOR_FORMAT_T colorFormat, int numCores, int &stride) {
    ivdext_create_ip_t s_create_ip = {};
    ivdext_create_op_t s_create_op = {};

    s_create_ip.s_ivd_create_ip_t.u4_size = sizeof(ivdext_create_ip_t);
    s_create_ip.s_ivd_create_ip_t.e_cmd = IVD_CMD_CREATE;
    s_create_ip.s_ivd_create_ip_t.u4_share_disp_buf = 0;
    s_create_ip.s_ivd_create_ip_t.e_output_format = colorFormat;
    s_create_ip.s_ivd_create_ip_t.pf_aligned_alloc = ivd_aligned_malloc;
    s_create_ip.s_ivd_create_ip_t.pf_aligned_free = ivd_aligned_free;
    s_create_ip.s_ivd_create_ip_t.pv_mem_ctxt = nullptr;
    s_create_op.s_ivd_create_op_t.u4_size = sizeof(ivdext_create_op_t);

    IV_API_CALL_STATUS_T status = ivdec_api_function(decHandle, &s_create_ip, &s_create_op);
    if (status != IV_SUCCESS) {
        ALOGE("error in %s: 0x%x", __func__, s_create_op.s_ivd_create_op_t.u4_error_code);
        return;
    }
    decHandle = (iv_obj_t *)s_create_op.s_ivd_create_op_t.pv_handle;
    decHandle->pv_fxns = (void *)ivdec_api_function;
    decHandle->u4_size = sizeof(iv_obj_t);

    stride = ALIGN128(stride); // Assuming width passed as stride initially, or logic handled in Helper
    setNumCores(decHandle, numCores);
}

void H264Traits::destroyDecoder(iv_obj_t *decHandle) {
    if (decHandle) {
        ivdext_delete_ip_t s_delete_ip = {};
        ivdext_delete_op_t s_delete_op = {};

        s_delete_ip.s_ivd_delete_ip_t.u4_size = sizeof(ivdext_delete_ip_t);
        s_delete_ip.s_ivd_delete_ip_t.e_cmd = IVD_CMD_DELETE;
        s_delete_op.s_ivd_delete_op_t.u4_size = sizeof(ivdext_delete_op_t);
        ivdec_api_function(decHandle, &s_delete_ip, &s_delete_op);
    }
}

void H264Traits::setParams(iv_obj_t *decHandle, size_t stride, IVD_VIDEO_DECODE_MODE_T dec_mode) {
    ih264d_ctl_set_config_ip_t s_h264d_set_dyn_params_ip = {};
    ih264d_ctl_set_config_op_t s_h264d_set_dyn_params_op = {};
    ivd_ctl_set_config_ip_t *ps_set_dyn_params_ip = &s_h264d_set_dyn_params_ip.s_ivd_ctl_set_config_ip_t;
    ivd_ctl_set_config_op_t *ps_set_dyn_params_op = &s_h264d_set_dyn_params_op.s_ivd_ctl_set_config_op_t;

    ps_set_dyn_params_ip->u4_size = sizeof(ih264d_ctl_set_config_ip_t);
    ps_set_dyn_params_ip->e_cmd = IVD_CMD_VIDEO_CTL;
    ps_set_dyn_params_ip->e_sub_cmd = IVD_CMD_CTL_SETPARAMS;
    ps_set_dyn_params_ip->u4_disp_wd = (UWORD32)stride;
    ps_set_dyn_params_ip->e_frm_skip_mode = IVD_SKIP_NONE;
    ps_set_dyn_params_ip->e_frm_out_mode = IVD_DISPLAY_FRAME_OUT;
    ps_set_dyn_params_ip->e_vid_dec_mode = dec_mode;
    ps_set_dyn_params_op->u4_size = sizeof(ih264d_ctl_set_config_op_t);

    ivdec_api_function(decHandle, ps_set_dyn_params_ip, ps_set_dyn_params_op);
}

void H264Traits::resetDecoder(iv_obj_t *decHandle) {
    ivd_ctl_reset_ip_t s_reset_ip = {};
    ivd_ctl_reset_op_t s_reset_op = {};

    s_reset_ip.u4_size = sizeof(ivd_ctl_reset_ip_t);
    s_reset_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_reset_ip.e_sub_cmd = IVD_CMD_CTL_RESET;
    s_reset_op.u4_size = sizeof(ivd_ctl_reset_op_t);
    ivdec_api_function(decHandle, &s_reset_ip, &s_reset_op);
}

void H264Traits::setNumCores(iv_obj_t *decHandle, int numCores) {
    ivdext_ctl_set_num_cores_ip_t s_set_num_cores_ip = {};
    ivdext_ctl_set_num_cores_op_t s_set_num_cores_op = {};

    s_set_num_cores_ip.u4_size = sizeof(ivdext_ctl_set_num_cores_ip_t);
    s_set_num_cores_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_set_num_cores_ip.e_sub_cmd = IVDEXT_CMD_CTL_SET_NUM_CORES;
    s_set_num_cores_ip.u4_num_cores = numCores;
    s_set_num_cores_op.u4_size = sizeof(ivdext_ctl_set_num_cores_op_t);

    ivdec_api_function(decHandle, &s_set_num_cores_ip, &s_set_num_cores_op);
}

IV_API_CALL_STATUS_T H264Traits::callApi(iv_obj_t *decHandle, void *ip, void *op) {
    return ivdec_api_function(decHandle, ip, op);
}

bool H264Traits::isKeyFrame(const uint8_t *frame, int inSize) {
    if (inSize < 5) return false;
    if (frame[0] == 0 && frame[1] == 0 && frame[2] == 0 && frame[3] == 1) {
        const bool forbiddenBitIsInvalid = 0x80 & frame[4];
        if (forbiddenBitIsInvalid) {
            return false;
        }
        uint8_t naluType = 0x1f & frame[4];
        if (naluType == 7 || naluType == 8) return true;
    }
    return false;
}

} // namespace android
