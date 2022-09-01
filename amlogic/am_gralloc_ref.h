/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#pragma once

#include <hardware/gralloc1.h>

/*NOTICE:
 * Pick three unused usage yet for v4l2 decoder buffer.
 * MESON_GRALLOC_USAGE_VIDEO_DECODER_QUARTER : 1/4 v4l2 decoder buffer
 * MESON_GRALLOC_USAGE_VIDEO_DECODER_ONE_SIXTEENTH : 1/16 v4l2 decoder buffer
 * MESON_GRALLOC_USAGE_VIDEO_DECODER_FULL : 1:1 v4l2 decoder buffer
 * Once the above three usage will be used by Gralloc, should fix here!
 */
#define MESON_GRALLOC_USAGE_VIDEO_DECODER_QUARTER GRALLOC1_CONSUMER_USAGE_PRIVATE_2
#define MESON_GRALLOC_USAGE_VIDEO_DECODER_ONE_SIXTEENTH GRALLOC1_CONSUMER_USAGE_CURSOR
#define MESON_GRALLOC_USAGE_VIDEO_DECODER_FULL GRALLOC1_PRODUCER_USAGE_SENSOR_DIRECT_DATA
