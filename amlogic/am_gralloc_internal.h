/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef AM_GRALLOC_EXT_INTERNAL_H
#define AM_GRALLOC_EXT_INTERNAL_H

#include "am_gralloc_ref.h"
#include <utils/NativeHandle.h>

#define OMX_VIDEOLAYER_ALLOC_BUFFER_WIDTH     192
#define OMX_VIDEOLAYER_ALLOC_BUFFER_HEIGHT    90

/*
For gralloc to check producer usage.
The usage is usage defined in gralloc.h/gralloc1.h.
*/
bool am_gralloc_is_omx_metadata_extend_usage(uint64_t usage);
bool am_gralloc_is_omx_osd_extend_usage(uint64_t usage);
bool am_gralloc_is_video_decoder_quarter_buffer_usage(uint64_t usage);
bool am_gralloc_is_video_decoder_one_sixteenth_buffer_usage(uint64_t usage);
bool am_gralloc_is_video_decoder_full_buffer_usage(uint64_t usage);
bool am_gralloc_is_video_decoder_OSD_buffer_usage(uint64_t usage);
bool am_gralloc_is_secure_extend_usage(uint64_t usage);

/* For v4l2 decoder, 1:1 buffer is UVM_IMM_ALLOC,
 * real width and height will be passed to gralloc,
 * but decoder need an aligned buffer size.
 */
bool need_do_width_height_align(uint64_t usage, int width, int height);


/*
For gralloc to set special buffer flag.
*/
int am_gralloc_get_omx_metadata_extend_flag();
int am_gralloc_get_coherent_extend_flag();
int am_gralloc_get_secure_extend_flag();

#endif/*AM_GRALLOC_EXT_INTERNAL_H*/
