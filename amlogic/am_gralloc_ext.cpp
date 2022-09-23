/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include "am_gralloc_ext.h"
#include <hardware/gralloc1.h>
#include <android/hardware/graphics/mapper/4.0/IMapper.h>
#include <gralloctypes/Gralloc4.h>
#include <aidl/arm/graphics/AmlMetadataType.h>
#include <aidl/arm/graphics/AmlMetadataType.h>
#include <buffer.h>
#include <gralloc/formats.h>


/*
Api default have upgrade to support gralloc 3.x.
For legacy gralloc, need force enable GRALLOC_USE_GRALLOC1_API in file or mk.
*/
//#define GRALLOC_USE_GRALLOC1_API 1
#include <cutils/properties.h>

using android::hardware::graphics::mapper::V4_0::Error;
using android::hardware::graphics::mapper::V4_0::IMapper;
using android::hardware::hidl_vec;
using android::gralloc4::encodeInt32;
using android::gralloc4::decodeInt32;


#define GRALLOC_AML_METADATA_TYPE_NAME "arm.graphics.AmlMetadataType"
const static IMapper::MetadataType AmlMetadataType_AM_OMX_TUNNEL{
    GRALLOC_AML_METADATA_TYPE_NAME,
    static_cast<int64_t>(aidl::arm::graphics::AmlMetadataType::AM_OMX_TUNNEL)
};

const static IMapper::MetadataType AmlMetadataType_AM_OMX_FLAG{
    GRALLOC_AML_METADATA_TYPE_NAME,
    static_cast<int64_t>(aidl::arm::graphics::AmlMetadataType::AM_OMX_FLAG)
};

const static IMapper::MetadataType AmlMetadataType_AM_OMX_VIDEO_TYPE{
    GRALLOC_AML_METADATA_TYPE_NAME,
    static_cast<int64_t>(aidl::arm::graphics::AmlMetadataType::AM_OMX_VIDEO_TYPE)
};

const static IMapper::MetadataType AmlMetadataType_AM_OMX_BUFFER_SEQUENCE{ GRALLOC_AML_METADATA_TYPE_NAME,
    static_cast<int64_t>(aidl::arm::graphics::AmlMetadataType::AM_OMX_BUFFER_SEQUENCE) };

static IMapper &get_service()
{
  static android::sp<IMapper> cached_service = IMapper::getService();
  return *cached_service;
}

static int am_get_metadata(IMapper &mapper, const native_handle_t * handle, IMapper::MetadataType type, int* value)
{
	void *handle_arg = const_cast<native_handle_t *>(handle);
	assert(handle_arg);
	assert(value);
	assert(decode);

	int err = 0;
    Error error;
    hidl_vec<uint8_t> metadata;
    mapper.get(handle_arg, type,
                [&](const auto& tmpError, const hidl_vec<uint8_t>& tmpVec) {
                    error = tmpError;
                    metadata = tmpVec;
                });
    if (error == Error::NONE) {
        err = decodeInt32(type, metadata, value);
    }
	return err;
}

static int am_set_metadata(IMapper &mapper, const native_handle_t * handle, IMapper::MetadataType type, const int value)
{
    void *handle_arg = const_cast<native_handle_t *>(handle);
    assert(handle_arg);
    assert(encode);
    int err = 0;
    hidl_vec<uint8_t> metadata;
    err = encodeInt32(type, value, &metadata);
    if (!err) {
        Error error = mapper.set(handle_arg, type, metadata);
        if (error != Error::NONE)
        {
            err = android::BAD_VALUE;
        }
    }
    return err;
}

bool am_gralloc_is_valid_graphic_buffer(
    const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return true;
    return false;
}

int am_gralloc_ext_get_ext_attr(const native_handle_t * hnd,
    IMapper::MetadataType type, int * val) {
    auto &mapper = get_service();
    int err = am_get_metadata(mapper, hnd, type, val);
    if (err != android::OK)
    {
        ALOGE("Failed to get metadata");
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    return GRALLOC1_ERROR_NONE;
}

int am_gralloc_ext_set_ext_attr(const native_handle_t * hnd,
    IMapper::MetadataType type, int val) {
    auto &mapper = get_service();
    int err = am_set_metadata(mapper, hnd, type, val);
    if (err != android::OK)
    {
        ALOGE("Failed to set metadata");
        return GRALLOC1_ERROR_BAD_HANDLE;
    }
    return GRALLOC1_ERROR_NONE;
}

#include <android/hardware/graphics/common/1.0/types.h>

using android::hardware::graphics::common::V1_0::BufferUsage;

uint64_t am_gralloc_get_omx_metadata_producer_usage() {
    return static_cast<uint64_t>(BufferUsage::VIDEO_DECODER);
}

uint64_t am_gralloc_get_omx_osd_producer_usage() {
    return static_cast<uint64_t>(BufferUsage::VIDEO_DECODER
        | BufferUsage::GPU_RENDER_TARGET);
}

bool am_gralloc_is_omx_metadata_producer(uint64_t usage) {
    if (!am_gralloc_is_omx_osd_producer(usage)) {
        uint64_t omx_metadata_usage = am_gralloc_get_omx_metadata_producer_usage();
        if (((usage & omx_metadata_usage) == omx_metadata_usage)
            && !(usage & BufferUsage::GPU_RENDER_TARGET)) {
            return true;
        }
    }

    return false;
}

uint64_t am_gralloc_get_video_decoder_quarter_buffer_usage() {
    uint64_t omx_metadata_usage = am_gralloc_get_omx_metadata_producer_usage();
    return (MESON_GRALLOC_USAGE_VIDEO_DECODER_QUARTER |
            omx_metadata_usage);
}

uint64_t am_gralloc_get_video_decoder_one_sixteenth_buffer_usage() {
    uint64_t omx_metadata_usage = am_gralloc_get_omx_metadata_producer_usage();
    return (MESON_GRALLOC_USAGE_VIDEO_DECODER_ONE_SIXTEENTH |
            omx_metadata_usage);
}

uint64_t am_gralloc_get_video_decoder_full_buffer_usage() {
    uint64_t omx_metadata_usage = am_gralloc_get_omx_metadata_producer_usage();
    return (MESON_GRALLOC_USAGE_VIDEO_DECODER_FULL |
            omx_metadata_usage);
}

uint64_t am_gralloc_get_video_decoder_OSD_buffer_usage() {
    uint64_t omx_osd_usage = am_gralloc_get_omx_osd_producer_usage();
    return (MESON_GRALLOC_USAGE_VIDEO_DECODER_FULL |
            omx_osd_usage);
}

bool am_gralloc_is_omx_osd_producer(uint64_t usage) {
    uint64_t omx_osd_usage = am_gralloc_get_omx_osd_producer_usage();
    if ((usage & omx_osd_usage) == omx_osd_usage) {
        return true;
    }

    return false;
}

int am_gralloc_get_format(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer) {
        ALOGV("[%s] format:%d", __FUNCTION__, buffer->format);
        return buffer->format;
    }

    return -ENOMEM;
}

int am_gralloc_get_buffer_fd(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer) {
        ALOGV("[%s] share_fd:%d", __FUNCTION__, buffer->share_fd);
        return buffer->share_fd;
    }

    return -1;
}

#ifdef GRALLOC_USE_GRALLOC1_API
int am_gralloc_get_stride_in_byte(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return buffer->byte_stride;

    return 0;
}
#else
int am_gralloc_get_stride_in_byte(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return buffer->plane_info[0].byte_stride;

    return 0;
}
#endif

int am_gralloc_get_stride_in_pixel(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return buffer->stride;

    return 0;
}

int am_gralloc_get_aligned_height(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return buffer->plane_info[0].alloc_height;

    return 0;
}

int am_gralloc_get_width(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return buffer->width;

    return 0;
}

int am_gralloc_get_height(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return buffer->height;

    return 0;
}

int am_gralloc_get_size(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return buffer->size;

    return 0;
}

uint64_t am_gralloc_get_producer_usage(
    const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return buffer->producer_usage;

    return 0;
}

uint64_t am_gralloc_get_consumer_usage(
    const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return buffer->consumer_usage;

    return 0;
}

uint64_t am_gralloc_get_usage(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer)
        return (am_gralloc_get_producer_usage(buffer) | am_gralloc_get_consumer_usage(buffer));

    return 0;
}

bool am_gralloc_is_secure_buffer(const native_handle_t *hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
     if (NULL == buffer)
        return true;

     if (buffer->flags & private_handle_t::PRIV_FLAGS_SECURE_PROTECTED)
         return true;

     return false;
}

bool am_gralloc_is_coherent_buffer(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (NULL == buffer)
        return false;

    if ((buffer->flags & private_handle_t::PRIV_FLAGS_CONTINUOUS_BUF)
            || (buffer->flags & private_handle_t::PRIV_FLAGS_USES_ION_DMA_HEAP)) {
        return true;
    }

    return false;
}

bool am_gralloc_is_overlay_buffer(const native_handle_t * hnd) {
     private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
     if (buffer && (buffer->flags & private_handle_t::PRIV_FLAGS_VIDEO_OVERLAY))
         return true;

     return false;
}

bool am_gralloc_is_omx_metadata_buffer(
    const native_handle_t * hnd) {
    private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer && (buffer->flags & private_handle_t::PRIV_FLAGS_VIDEO_OMX)) {
        int val = 0;
        am_gralloc_ext_get_ext_attr(hnd,
            AmlMetadataType_AM_OMX_FLAG, &val);
        if (val != AM_PRIV_ATTR_OMX_V4L_PRODUCER)
            return true;
    }
    return false;
 }

bool am_gralloc_is_omx_v4l_buffer(
    const native_handle_t * hnd) {
    private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;

    if (buffer && (buffer->flags & private_handle_t::PRIV_FLAGS_VIDEO_OMX)) {
        int val = 0;
        am_gralloc_ext_get_ext_attr(buffer,
            AmlMetadataType_AM_OMX_FLAG, &val);
        if (val == AM_PRIV_ATTR_OMX_V4L_PRODUCER)
            return true;
    }
    return false;
 }

bool am_gralloc_is_uvm_dma_buffer(const native_handle_t *hnd) {
    private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;

    if (buffer && (buffer->flags & private_handle_t::PRIV_FLAGS_UVM_BUFFER))
        return true;

    return false;
}

 int am_gralloc_get_omx_metadata_tunnel(
    const native_handle_t * hnd, int * tunnel) {
    private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    int ret = GRALLOC1_ERROR_NONE;
    if (buffer) {
        int val;
        ret = am_gralloc_ext_get_ext_attr(hnd,
            AmlMetadataType_AM_OMX_TUNNEL, &val);
        if (ret == GRALLOC1_ERROR_NONE) {
            if (val == 1)
                *tunnel = 1;
            else
                *tunnel = 0;
        }
    } else {
        ret = GRALLOC1_ERROR_BAD_HANDLE;
    }

    return ret;
}

 int am_gralloc_set_omx_metadata_tunnel(
    const native_handle_t * hnd, int tunnel) {
     private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
     int ret = GRALLOC1_ERROR_NONE;

    if (buffer) {
        ret = am_gralloc_ext_set_ext_attr(buffer,
            AmlMetadataType_AM_OMX_TUNNEL, tunnel);
    } else {
        ret = GRALLOC1_ERROR_BAD_HANDLE;
    }

    return ret;
}

 int am_gralloc_get_omx_video_type(
    const native_handle_t * hnd, int * video_type) {
    private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    int ret = GRALLOC1_ERROR_NONE;
    if (buffer) {
        int val;
        ret = am_gralloc_ext_get_ext_attr(hnd,
            AmlMetadataType_AM_OMX_VIDEO_TYPE, &val);
        if (ret == GRALLOC1_ERROR_NONE) {
            *video_type = val;
        }
    } else {
        ret = GRALLOC1_ERROR_BAD_HANDLE;
    }

    return ret;
}

 int am_gralloc_set_omx_video_type(
    const native_handle_t * hnd, int video_type) {
     private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
     int ret = GRALLOC1_ERROR_NONE;

    if (buffer) {
        ret = am_gralloc_ext_set_ext_attr(buffer,
            AmlMetadataType_AM_OMX_VIDEO_TYPE, video_type);
    } else {
        ret = GRALLOC1_ERROR_BAD_HANDLE;
    }

    return ret;
}

typedef struct am_sideband_handle {
   native_handle_t base;
   unsigned int id;
   int flags;
   int channel;
} am_sideband_handle_t;

#define AM_SIDEBAND_HANDLE_NUM_INT (3)
#define AM_SIDEBAND_HANDLE_NUM_FD (0)
#define AM_SIDEBAND_IDENTIFIER (0xabcdcdef)

native_handle_t * am_gralloc_create_sideband_handle(int type, int channel) {
    am_sideband_handle_t * pHnd = (am_sideband_handle_t *)
        native_handle_create(AM_SIDEBAND_HANDLE_NUM_FD,
        AM_SIDEBAND_HANDLE_NUM_INT);
    pHnd->id = AM_SIDEBAND_IDENTIFIER;

    if (type == AM_TV_SIDEBAND) {
        pHnd->flags = private_handle_t::PRIV_FLAGS_VIDEO_OVERLAY;
    } else if (type == AM_OMX_SIDEBAND) {
        pHnd->flags = private_handle_t::PRIV_FLAGS_VIDEO_OMX;
    } else if (type == AM_AMCODEX_SIDEBAND) {
        pHnd->flags = private_handle_t::PRIV_FLAGS_VIDEO_AMCODEX;
    } else if (type == AM_FIXED_TUNNEL) {
        pHnd->flags = private_handle_t::PRIV_FLAGS_VIDEO_TUNNEL;
    }
    pHnd->channel = channel;

    return (native_handle_t *)pHnd;
}

int am_gralloc_destroy_sideband_handle(native_handle_t * hnd) {
    if (hnd) {
        native_handle_delete(hnd);
    }

    return 0;
}

 int am_gralloc_get_sideband_channel(
    const native_handle_t * hnd, int * channel) {
    if (!hnd || hnd->version != sizeof(native_handle_t)
        || hnd->numInts != AM_SIDEBAND_HANDLE_NUM_INT || hnd->numFds != AM_SIDEBAND_HANDLE_NUM_FD) {
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    am_sideband_handle_t * buffer = (am_sideband_handle_t *)(hnd);
    if (buffer->id != AM_SIDEBAND_IDENTIFIER)
        return GRALLOC1_ERROR_BAD_HANDLE;

    int ret = GRALLOC1_ERROR_NONE;
    if (buffer) {
        if (buffer->flags == private_handle_t::PRIV_FLAGS_VIDEO_TUNNEL) {
            *channel = buffer->channel;
        } else {
            if (buffer->channel == AM_VIDEO_DEFAULT || buffer->channel == AM_VIDEO_DEFAULT_LEGACY) {
                *channel = AM_VIDEO_DEFAULT;
            } else {
                *channel = AM_VIDEO_EXTERNAL;
            }
        }
    } else {
        ret = GRALLOC1_ERROR_BAD_HANDLE;
    }

    return ret;
}

int am_gralloc_get_sideband_type(const native_handle_t* hnd, int* type) {
    if (!hnd || hnd->version != sizeof(native_handle_t)
            || hnd->numInts != AM_SIDEBAND_HANDLE_NUM_INT
            || hnd->numFds != AM_SIDEBAND_HANDLE_NUM_FD) {
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    const am_sideband_handle_t * buffer = (am_sideband_handle_t *)(hnd);
    if (buffer->id != AM_SIDEBAND_IDENTIFIER)
        return GRALLOC1_ERROR_BAD_HANDLE;

    int ret = GRALLOC1_ERROR_NONE;
    if (buffer) {
        if (buffer->flags & private_handle_t::PRIV_FLAGS_VIDEO_OVERLAY) {
            *type = AM_TV_SIDEBAND;
        } else if (buffer->flags & private_handle_t::PRIV_FLAGS_VIDEO_OMX) {
            *type = AM_OMX_SIDEBAND;
        } else if (buffer->flags & private_handle_t::PRIV_FLAGS_VIDEO_AMCODEX) {
            *type = AM_AMCODEX_SIDEBAND;
        } else if (buffer->flags & private_handle_t::PRIV_FLAGS_VIDEO_TUNNEL) {
            *type = AM_FIXED_TUNNEL;
        } else {
            ret = GRALLOC1_ERROR_BAD_HANDLE;
        }
    }
    return ret;
}

#ifdef GRALLOC_USE_GRALLOC1_API
int am_gralloc_get_vpu_afbc_mask(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;

    if (buffer) {
        uint64_t internalFormat = buffer->internal_format;
        int afbcFormat = 0;

        if (internalFormat & MALI_GRALLOC_INTFMT_AFBCENABLE_MASK) {
            afbcFormat |=
                (VPU_AFBC_EN | VPU_AFBC_YUV_TRANSFORM |VPU_AFBC_BLOCK_SPLIT);

            if (internalFormat & MALI_GRALLOC_INTFMT_AFBC_WIDEBLK) {
                afbcFormat |= VPU_AFBC_SUPER_BLOCK_ASPECT;
            }

            #if 0
            if (internalFormat & MALI_GRALLOC_INTFMT_AFBC_SPLITBLK) {
                afbcFormat |= VPU_AFBC_BLOCK_SPLIT;
            }
            #endif

            if (internalFormat & MALI_GRALLOC_INTFMT_AFBC_TILED_HEADERS) {
                afbcFormat |= VPU_AFBC_TILED_HEADER_EN;
            }

            #if 0
            if (gralloc_buffer_attr_map(buffer, 0) >= 0) {
                int tmp=0;
                if (gralloc_buffer_attr_read(buffer, GRALLOC_ARM_BUFFER_ATTR_AFBC_YUV_TRANS, &tmp) >= 0) {
                    if (tmp != 0) {
                        afbcFormat |= VPU_AFBC_YUV_TRANSFORM;
                    }
                }
            }
            #endif
        }
        return afbcFormat;
    }

    return 0;
}
#else
int am_gralloc_get_vpu_afbc_mask(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;

    if (buffer) {
        uint64_t internalFormat = buffer->alloc_format;
        int afbcFormat = 0;

        if (internalFormat & MALI_GRALLOC_INTFMT_AFBCENABLE_MASK) {
            afbcFormat |=
                (VPU_AFBC_EN | VPU_AFBC_YUV_TRANSFORM |VPU_AFBC_BLOCK_SPLIT);

            if (internalFormat & MALI_GRALLOC_INTFMT_AFBC_WIDEBLK) {
                afbcFormat |= VPU_AFBC_SUPER_BLOCK_ASPECT;
            }

            #if 0
            if (internalFormat & MALI_GRALLOC_INTFMT_AFBC_SPLITBLK) {
                afbcFormat |= VPU_AFBC_BLOCK_SPLIT;
            }
            #endif

            if (internalFormat & MALI_GRALLOC_INTFMT_AFBC_TILED_HEADERS) {
                afbcFormat |= VPU_AFBC_TILED_HEADER_EN;
            }

            #if 0
            if (gralloc_buffer_attr_map(buffer, 0) >= 0) {
                int tmp=0;
                if (gralloc_buffer_attr_read(buffer, GRALLOC_ARM_BUFFER_ATTR_AFBC_YUV_TRANS, &tmp) >= 0) {
                    if (tmp != 0) {
                        afbcFormat |= VPU_AFBC_YUV_TRANSFORM;
                    }
                }
            }
            #endif
        }
        return afbcFormat;
    }

    return 0;
}

int am_gralloc_get_omx_v4l_file(const native_handle_t * hnd) {
    private_handle_t const* buffer = hnd ? private_handle_t::downcast(hnd) : NULL;

    if (buffer && buffer->am_extend_type == 0) {
        return buffer->am_extend_fd;
    } else {
        ALOGE("Current buffer is not OMX_V4L extend.");
    }

    return -1;
}

int am_gralloc_attr_set_omx_v4l_producer_flag(
    native_handle_t * hnd) {
    private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;

    if (buffer) {
        int val = AM_PRIV_ATTR_OMX_V4L_PRODUCER;
        am_gralloc_ext_set_ext_attr(buffer,
            AmlMetadataType_AM_OMX_FLAG, val);
    }

    return -1;
}

int am_gralloc_attr_set_omx_pts_producer_flag(
    native_handle_t * hnd) {
    private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;

    if (buffer) {
        int val = AM_PRIV_ATTR_OMX_PTS_PRODUCER;
        am_gralloc_ext_set_ext_attr(buffer,
            AmlMetadataType_AM_OMX_FLAG, val);
    }

    return -1;
}

#endif

uint64_t am_gralloc_get_enc_coherent_usage() {
    return am_gralloc_get_omx_osd_producer_usage();
}


int am_gralloc_set_ext_attr(const native_handle_t * hnd, uint32_t attr, int val) {
    private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    int ret = GRALLOC1_ERROR_NONE;

    if (buffer) {
    IMapper::MetadataType type;
    switch (attr) {
        case GRALLOC_BUFFER_ATTR_AM_OMX_TUNNEL:
            type = AmlMetadataType_AM_OMX_TUNNEL;
            break;
        case GRALLOC_BUFFER_ATTR_AM_OMX_FLAG:
            type = AmlMetadataType_AM_OMX_FLAG;
            break;
        case GRALLOC_BUFFER_ATTR_AM_OMX_VIDEO_TYPE:
            type = AmlMetadataType_AM_OMX_VIDEO_TYPE;
            break;
        case GRALLOC_BUFFER_ATTR_AM_OMX_BUFFER_SEQUENCE:
            /*GRALLOC_BUFFER_ATTR_AM_OMX_BUFFER_SEQUENCE: -1 is invalid value*/
            if (val == -1) {
                ALOGE("Set invalid value for OMX_BUFFER_SEQUENCE");
                return GRALLOC1_ERROR_BAD_VALUE;
            } else {
                type = AmlMetadataType_AM_OMX_BUFFER_SEQUENCE;
                break;
            }
        default:
            ALOGE("set invalid attr :%d", attr);
            return GRALLOC1_ERROR_BAD_VALUE;
    }
        ret = am_gralloc_ext_set_ext_attr(hnd, type, val);
    } else {
        ret = GRALLOC1_ERROR_BAD_HANDLE;
    }

    return ret;
}

bool am_gralloc_get_omx_buffer_sequence(const native_handle_t * hnd, int *val) {
    private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
    if (buffer && (buffer->flags & private_handle_t::PRIV_FLAGS_VIDEO_OMX)) {
        int ret = GRALLOC1_ERROR_NONE;
        int omx_buffer_sequence;
        ret = am_gralloc_ext_get_ext_attr(hnd,
                AmlMetadataType_AM_OMX_BUFFER_SEQUENCE, &omx_buffer_sequence);
        if (ret == GRALLOC1_ERROR_NONE) {
            *val = omx_buffer_sequence;
            return true;
        }
    }

    return false;
}

