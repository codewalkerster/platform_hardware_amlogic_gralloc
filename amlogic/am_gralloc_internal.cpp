/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include "am_gralloc_internal.h"
#include <sys/ioctl.h>
#include "core/buffer.h"
#include "am_gralloc_uvm_ext.h"
#include "log.h"

#define V4LVIDEO_IOC_MAGIC  'I'
#define V4LVIDEO_IOCTL_ALLOC_FD   _IOW(V4LVIDEO_IOC_MAGIC, 0x02, int)

#define UNUSED(x) (void)x
const static int AML_GRALLOC_SLOT_NUM = 32;

bool am_gralloc_is_omx_metadata_extend_usage(
    uint64_t usage) {
    uint64_t omx_metadata_usage =
          GRALLOC1_PRODUCER_USAGE_VIDEO_DECODER;

    if (((usage & omx_metadata_usage) == omx_metadata_usage)
        && !(usage & GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET)) {
        return true;
    }

    return false;
}

bool am_gralloc_is_omx_osd_extend_usage(uint64_t usage) {
    uint64_t omx_osd_usage = GRALLOC1_PRODUCER_USAGE_VIDEO_DECODER
        |GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;
    if ((usage & omx_osd_usage) == omx_osd_usage) {
        return true;
    }
    return false;
}

/* usage for v4l2 decoder 1/4 buffer */
bool am_gralloc_is_video_decoder_quarter_buffer_usage(
    uint64_t usage) {
    uint64_t video_decoder_quarter_buffer_usage = MESON_GRALLOC_USAGE_VIDEO_DECODER_QUARTER;
    if (am_gralloc_is_omx_metadata_extend_usage(usage)
        && ((usage & video_decoder_quarter_buffer_usage) == video_decoder_quarter_buffer_usage)) {
        return true;
    }
    return false;
}

/* usage for v4l2 decoder 1/16 buffer */
bool am_gralloc_is_video_decoder_one_sixteenth_buffer_usage(
    uint64_t usage) {
    uint64_t video_decoder_one_sixteenth_buffer_usage = MESON_GRALLOC_USAGE_VIDEO_DECODER_ONE_SIXTEENTH;
    if (am_gralloc_is_omx_metadata_extend_usage(usage)
        && ((usage & video_decoder_one_sixteenth_buffer_usage) == video_decoder_one_sixteenth_buffer_usage)) {
        return true;
    }
    return false;
}

/* usage for v4l2 decoder H264 surfaceview buffer */
bool am_gralloc_is_video_decoder_full_buffer_usage(
    uint64_t usage) {
    uint64_t video_decoder_full_buffer_usage = MESON_GRALLOC_USAGE_VIDEO_DECODER_FULL;
    if (am_gralloc_is_omx_metadata_extend_usage(usage)
        && ((usage & video_decoder_full_buffer_usage) == video_decoder_full_buffer_usage)) {
        return true;
    }
    return false;
}

/* usage for v4l2 decoder H264 surfacetexture buffer */
bool am_gralloc_is_video_decoder_OSD_buffer_usage(
    uint64_t usage) {
    uint64_t video_decoder_osd_buffer_usage = MESON_GRALLOC_USAGE_VIDEO_DECODER_FULL;
    if (am_gralloc_is_omx_osd_extend_usage(usage)
        && ((usage & video_decoder_osd_buffer_usage) == video_decoder_osd_buffer_usage)) {
        return true;
    }
    return false;
}

bool am_gralloc_is_video_decoder_replace_buffer_usage(
    uint64_t usage) {
    uint64_t video_decoder_replace_buffer_usage = MESON_GRALLOC_USAGE_DECODER_BUF_REPLACE;
    if (am_gralloc_is_omx_metadata_extend_usage(usage)
        && ((usage & video_decoder_replace_buffer_usage) == video_decoder_replace_buffer_usage)) {
        return true;
    }
    return false;
}

bool am_gralloc_is_secure_extend_usage(
    uint64_t usage) {
    if (usage & GRALLOC1_PRODUCER_USAGE_PROTECTED) {
        return true;
    }

    return false;
}

int am_gralloc_get_omx_metadata_extend_flag() {
    return private_handle_t::PRIV_FLAGS_VIDEO_OVERLAY
        | private_handle_t::PRIV_FLAGS_VIDEO_OMX;
}

int am_gralloc_get_coherent_extend_flag() {
    return private_handle_t::PRIV_FLAGS_USES_ION_DMA_HEAP
        | private_handle_t::PRIV_FLAGS_CONTINUOUS_BUF;
}

int am_gralloc_get_secure_extend_flag() {
    return private_handle_t::PRIV_FLAGS_SECURE_PROTECTED
        | private_handle_t::PRIV_FLAGS_CONTINUOUS_BUF;
}

bool need_do_width_height_align(uint64_t usage,
    int width, int height) {
    if ((am_gralloc_is_omx_osd_extend_usage(usage) &&
        width == 100 && height == 100)
        || (am_gralloc_is_video_decoder_full_buffer_usage(usage))
        || (am_gralloc_is_video_decoder_OSD_buffer_usage(usage)))
        return true;
    else
        return false;
}

bool am_gralloc_get_para_from_node(uint32_t slot_id, struct gralloc_decoder_para *para)
{
    if (slot_id >= AML_GRALLOC_SLOT_NUM) {
        ALOGE("%s: slot_id(%d) invalid!", __func__, slot_id);
        return false;
    }

    uvm_decoder_para uvm_para{slot_id};
    if (am_gralloc_get_uvm_decoder_para(&uvm_para) < 0) {
        AML_GRALLOC_LOGI("%s: get decoder para from UVM failed, may be not support! slot_id=%d", __func__, slot_id);
        return false;
    }

    para->width   = uvm_para.width;
    para->height  = uvm_para.height;
    para->w_align = uvm_para.w_align;
    para->h_align = uvm_para.h_align;
    para->size    = uvm_para.size;
    return true;
}

