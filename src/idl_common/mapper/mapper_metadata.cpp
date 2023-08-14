/*
 * Copyright (C) 2020-2023 Arm Limited.
 *
 * Copyright 2016 The Android Open Source Project
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

#include "mapper/mapper_metadata.h"
#include "idl_common/shared_metadata.h"
#include "core/format_info.h"
#include "core/drm_utils.h"
#include "core/buffer_allocation.h"
#include "core/buffer.h"
#include "log.h"
#include "gralloctypes/Gralloc4.h"
#include "mapper/mapper_common.hpp"
#include "mapper/mapper_types.hpp"
#include <vector>

namespace arm
{
namespace mapper
{
namespace common
{

using aidl::android::hardware::graphics::common::BlendMode;
using aidl::android::hardware::graphics::common::Cta861_3;
using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::Rect;
using aidl::android::hardware::graphics::common::Smpte2086;
using aidl::android::hardware::graphics::common::StandardMetadataType;
using aidl::arm::graphics::ArmMetadataType;
using aidl::arm::graphics::AmlMetadataType;
using android::hardware::graphics::common::V1_2::BufferUsage;

static aidl::arm::graphics::DataType data_type_internal_to_aidl(mali_gralloc_format_data_type data_type)
{
	switch (data_type)
	{
	case mali_gralloc_format_data_type::UNORM:
		return (aidl::arm::graphics::DataType::UNORM);
		break;
	case mali_gralloc_format_data_type::SNORM:
		return (aidl::arm::graphics::DataType::SNORM);
		break;
	case mali_gralloc_format_data_type::UINT:
		return (aidl::arm::graphics::DataType::UINT);
		break;
	case mali_gralloc_format_data_type::SINT:
		return (aidl::arm::graphics::DataType::SINT);
		break;
	default:
		return (aidl::arm::graphics::DataType::UNKNOWN);
		break;
	}
}

static void get_plane_fds(const private_handle_t *hnd, std::vector<int64_t> *fds)
{
	const int num_planes = hnd->get_num_planes();
	fds->resize(num_planes, static_cast<int64_t>(hnd->share_fd));
}

mapper_error get_metadata(const private_handle_t *handle, const metadata_descriptor &metadata,
                          std::vector<uint8_t> &output, metadata_encoder encode_fn)
{
	/* Default case in case metadata is not found */
	mapper_error err = mapper_error::UNSUPPORTED;

	if (metadata.is_standard_metadata_type())
	{
		switch (metadata.get_standard_metadata_type_value())
		{
		case StandardMetadataType::BUFFER_ID:
			err = encode_fn(&handle->backing_store_id, &output);
			break;
		case StandardMetadataType::NAME:
		{
			auto import = handle_cast<imported_handle>(handle);
			if (import == nullptr)
			{
				MALI_GRALLOC_LOGE("get() called on raw handle");
				err = mapper_error::BAD_VALUE;
				break;
			}

			std::string name;
			get_name(import, &name);
			err = encode_fn(&name, &output);
			break;
		}
		case StandardMetadataType::WIDTH:
		{
			uint64_t width = handle->width;
			err = encode_fn(&width, &output);
			break;
		}
		case StandardMetadataType::HEIGHT:
		{
			uint64_t height = handle->height;
			err = encode_fn(&height, &output);
			break;
		}
		case StandardMetadataType::LAYER_COUNT:
			err = encode_fn(&handle->layer_count, &output);
			break;
		case StandardMetadataType::PIXEL_FORMAT_REQUESTED:
			err = encode_fn(&handle->req_format, &output);
			break;
		case StandardMetadataType::PIXEL_FORMAT_FOURCC:
		{
			uint32_t fourcc = drm_fourcc_from_handle(handle);
			err = encode_fn(&fourcc, &output);
			break;
		}
		case StandardMetadataType::PIXEL_FORMAT_MODIFIER:
		{
			auto mod = drm_modifier_from_handle(handle);
			err = encode_fn(&mod, &output);
			break;
		}
		case StandardMetadataType::USAGE:
		{
			uint64_t usage = handle->consumer_usage | handle->producer_usage;
			err = encode_fn(&usage, &output);
			break;
		}
		case StandardMetadataType::ALLOCATION_SIZE:
		{
			uint64_t handle_size = handle->size;
			err = encode_fn(&handle_size, &output);
			break;
		}
		case StandardMetadataType::PROTECTED_CONTENT:
		{
			/* This is set to 1 if the buffer has protected content. */
			const uint64_t is_protected =
			    (((handle->consumer_usage | handle->producer_usage) & BufferUsage::PROTECTED) == 0) ? 0 : 1;
			err = encode_fn(&is_protected, &output);
			break;
		}
		case StandardMetadataType::COMPRESSION:
		{
			ExtendableType compression;
			const auto internal_format = handle->alloc_format;
			if (internal_format.is_afbc())
			{
				compression = Compression_AFBC;
			}
			else if (internal_format.is_afrc())
			{
				compression = Compression_AFRC;
			}
			else
			{
				compression = android::gralloc4::Compression_None;
			}
			err = encode_fn(&compression, &output);
			break;
		}
		case StandardMetadataType::INTERLACED:
			err = encode_fn(&android::gralloc4::Interlaced_None, &output);
			break;
		case StandardMetadataType::CHROMA_SITING:
		{
			auto import = handle_cast<imported_handle>(handle);
			if (import == nullptr)
			{
				MALI_GRALLOC_LOGE("get() called on raw handle");
				err = mapper_error::BAD_VALUE;
				break;
			}

			const auto *format_info = handle->alloc_format.get_base_info();
			if (format_info == nullptr)
			{
				err = mapper_error::BAD_VALUE;
				break;
			}

			std::optional<ExtendableType> chroma_siting;
			ExtendableType chroma_siting_default = android::gralloc4::ChromaSiting_None;
			if (format_info->is_yuv)
			{
				chroma_siting_default = android::gralloc4::ChromaSiting_Unknown;
			}

			get_chroma_siting(import, &chroma_siting);

			auto chroma_value = chroma_siting.value_or(chroma_siting_default);
			err = encode_fn(&chroma_value, &output);
			break;
		}
		case StandardMetadataType::PLANE_LAYOUTS:
		{
			std::vector<PlaneLayout> layouts;
			err = get_plane_layouts(handle, &layouts);
			if (err == mapper_error::NONE)
			{
				err = encode_fn(&layouts, &output);
			}
			break;
		}
		case StandardMetadataType::DATASPACE:
		{
			auto import = handle_cast<imported_handle>(handle);
			if (import == nullptr)
			{
				MALI_GRALLOC_LOGE("get() called on raw handle");
				err = mapper_error::BAD_VALUE;
				break;
			}

			std::optional<Dataspace> dataspace;
			get_dataspace(import, &dataspace);

			auto dataspace_value = dataspace.value_or(Dataspace::UNKNOWN);
			err = encode_fn(&dataspace_value, &output);
			break;
		}
		case StandardMetadataType::BLEND_MODE:
		{
			auto import = handle_cast<imported_handle>(handle);
			if (import == nullptr)
			{
				MALI_GRALLOC_LOGE("get() called on raw handle");
				err = mapper_error::BAD_VALUE;
				break;
			}

			std::optional<BlendMode> blend_mode;
			get_blend_mode(import, &blend_mode);

			auto blend_value = blend_mode.value_or(BlendMode::INVALID);
			err = encode_fn(&blend_value, &output);
			break;
		}
		case StandardMetadataType::CROP:
		{
			auto import = handle_cast<imported_handle>(handle);
			if (import == nullptr)
			{
				MALI_GRALLOC_LOGE("get() called on raw handle");
				err = mapper_error::BAD_VALUE;
				break;
			}

			const int num_planes = handle->get_num_planes();
			std::vector<Rect> crops(num_planes);
			for (size_t plane_index = 0; plane_index < num_planes; ++plane_index)
			{
				/* Set the default crop rectangle. Android mandates that it must fit [0, 0, widthInSamples, heightInSamples]
				 * We always require using the requested width and height for the crop rectangle size.
				 * For planes > 0 the size might need to be scaled, but since we only use plane[0] for crop set it to the
				 * Android default of [0, 0, widthInSamples, heightInSamples] for other planes.
				 */
				Rect rect = { .left = 0,
					          .top = 0,
					          .right = static_cast<int32_t>(handle->plane_info[plane_index].alloc_width),
					          .bottom = static_cast<int32_t>(handle->plane_info[plane_index].alloc_height) };
				if (plane_index == 0)
				{
					std::optional<Rect> crop_rect;
					get_crop_rect(import, &crop_rect);
					if (crop_rect.has_value())
					{
						rect = crop_rect.value();
					}
					else
					{
						rect = { .left = 0, .top = 0, .right = handle->width, .bottom = handle->height };
					}
				}
				crops[plane_index] = rect;
			}
			err = encode_fn(&crops, &output);
			break;
		}
		case StandardMetadataType::SMPTE2086:
		{
			auto import = handle_cast<imported_handle>(handle);
			if (import == nullptr)
			{
				MALI_GRALLOC_LOGE("get() called on raw handle");
				err = mapper_error::BAD_VALUE;
				break;
			}

			std::optional<Smpte2086> smpte2086;
			get_smpte2086(import, &smpte2086);
			err = encode_fn(&smpte2086, &output);
			break;
		}
		case StandardMetadataType::CTA861_3:
		{
			auto import = handle_cast<imported_handle>(handle);
			if (import == nullptr)
			{
				MALI_GRALLOC_LOGE("get() called on raw handle");
				err = mapper_error::BAD_VALUE;
				break;
			}

			std::optional<Cta861_3> cta861_3;
			get_cta861_3(import, &cta861_3);
			err = encode_fn(&cta861_3, &output);
			break;
		}
		case StandardMetadataType::SMPTE2094_40:
		{
			auto import = handle_cast<imported_handle>(handle);
			if (import == nullptr)
			{
				MALI_GRALLOC_LOGE("get() called on raw handle");
				err = mapper_error::BAD_VALUE;
				break;
			}

			std::optional<std::vector<uint8_t>> smpte2094_40;
			get_smpte2094_40(import, &smpte2094_40);
			err = encode_fn(&smpte2094_40, &output);
			break;
		}
#if PLATFORM_SDK_VERSION >= 33
		case StandardMetadataType::SMPTE2094_10:
		{
			auto import = handle_cast<imported_handle>(handle);
			if (import == nullptr)
			{
				MALI_GRALLOC_LOGE("get() called on raw handle");
				err = mapper_error::BAD_VALUE;
				break;
			}

			std::optional<std::vector<uint8_t>> smpte2094_10;
			get_smpte2094_10(import, &smpte2094_10);
			err = encode_fn(&smpte2094_10, &output);
			break;
		}
#endif
#if defined(GRALLOC_STABLEC_MAPPER_ENABLED) && GRALLOC_STABLEC_MAPPER_ENABLED == 1
		case StandardMetadataType::STRIDE:
		{
			uint32_t stride = handle->stride;
			err = encode_fn(&stride, &output);
			break;
		}

#endif
		case StandardMetadataType::INVALID:
		default:
			err = mapper_error::UNSUPPORTED;
		}
	}
	else if (metadata.is_arm_metadata_type())
	{
		switch (metadata.get_arm_metadata_type_value())
		{
		case ArmMetadataType::PLANE_FDS:
		{
			std::vector<int64_t> fds;
			get_plane_fds(handle, &fds);
			err = encode_fn(&fds, &output);
			break;
		}
		case ArmMetadataType::FORMAT_DATA_TYPE:
		{
			int64_t data_type =
			    static_cast<int64_t>(data_type_internal_to_aidl(handle->alloc_format.get_format_data_type()));
			err = encode_fn(&data_type, &output);
			break;
		}
		default:
			err = mapper_error::BAD_VALUE;
		}
	}
#ifdef GRALLOC_AML_EXTEND
	else if (metadata.is_aml_metadata_type())
	{
		switch (metadata.get_aml_metadata_type_value())
		{
		case AmlMetadataType::AM_OMX_TUNNEL:
		{
			int32_t am_omx_tunnel;
			auto import = handle_cast<imported_handle>(handle);
			get_omx_tunnel(import, &am_omx_tunnel);
			err = encode_fn(&am_omx_tunnel, &output);
			break;
		}
		case AmlMetadataType::AM_OMX_FLAG:
		{
			int32_t am_omx_flag;
			auto import = handle_cast<imported_handle>(handle);
			get_omx_flag(import, &am_omx_flag);
			err = encode_fn(&am_omx_flag, &output);
			break;
		}
		case AmlMetadataType::AM_OMX_VIDEO_TYPE:
		{
			int32_t am_omx_video_type;
			auto import = handle_cast<imported_handle>(handle);
			get_omx_video_type(import, &am_omx_video_type);
			err = encode_fn(&am_omx_video_type, &output);
			break;
		}
		case AmlMetadataType::AM_OMX_BUFFER_SEQUENCE:
		{
			int32_t am_omx_buffer_sequence;
			auto import = handle_cast<imported_handle>(handle);
			get_omx_buffer_sequence(import, &am_omx_buffer_sequence);
			err = encode_fn(&am_omx_buffer_sequence, &output);
			break;
		}
		default:
			err = mapper_error::BAD_VALUE;
		}
	}
#endif

	return err;
}

static bool isSupportedDataSpace(Dataspace dataspace)
{
	uint32_t standard = static_cast<android_dataspace_t>(dataspace) & HAL_DATASPACE_STANDARD_MASK;
	uint32_t range = static_cast<android_dataspace_t>(dataspace) & HAL_DATASPACE_RANGE_MASK;

	if (standard > 0 || range > 0)
		MALI_GRALLOC_LOGV("%s DATASPACE: standard [%d] range [%d]", __FUNCTION__, standard, range);

	switch (standard)
	{
	case HAL_DATASPACE_STANDARD_BT601_625:
	case HAL_DATASPACE_STANDARD_BT601_525:
	case HAL_DATASPACE_STANDARD_BT709:
	case HAL_DATASPACE_STANDARD_BT2020:
	case HAL_DATASPACE_STANDARD_DCI_P3:
		return true;
	case HAL_DATASPACE_UNKNOWN:
		switch (static_cast<android_dataspace_t>(dataspace) & 0xffff)
		{
		case HAL_DATASPACE_UNKNOWN:
			return false;
		default:
			return true;
		}
		break;
	default:
		ALOGE("Unsupported dataspace standard (%" PRIu32 ")", standard);
		return false;
	}
}

mapper_error set_metadata(const imported_handle *handle, const metadata_descriptor &metadata, const uint8_t *data,
                          size_t data_size, metadata_decoder decode_fn)
{
	mapper_error err = mapper_error::NONE;

	if (metadata.is_standard_metadata_type())
	{
		switch (metadata.get_standard_metadata_type_value())
		{
		case StandardMetadataType::DATASPACE:
		{
			Dataspace dataspace;
			std::optional<Dataspace> curDataspace = std::nullopt;

			err = decode_fn(data, data_size, &dataspace);
			if (err == mapper_error::NONE)
			{
				get_dataspace(handle, &curDataspace);
				if (curDataspace.has_value() && curDataspace.value() == dataspace)
				{
					break;
				}
				AML_GRALLOC_LOGI("%s DATASPACE:0x%08x", __FUNCTION__, dataspace);
				//android::CallStack c(LOG_TAG);
				if (!isSupportedDataSpace(dataspace))
				{
					return mapper_error::UNSUPPORTED;
				}
				set_dataspace(handle, dataspace);
			}
			break;
		}
		case StandardMetadataType::CHROMA_SITING:
		{
			const auto *format_info = handle->alloc_format.get_base_info();
			if (format_info == nullptr)
			{
				err = mapper_error::BAD_VALUE;
				break;
			}

			ExtendableType chroma_siting;
			err = decode_fn(data, data_size, &chroma_siting);
			if (err == mapper_error::NONE)
			{
				if ((chroma_siting.name == GRALLOC4_STANDARD_CHROMA_SITING ||
				     chroma_siting.name == GRALLOC_ARM_CHROMA_SITING_TYPE_NAME))
				{
					set_chroma_siting(handle, chroma_siting);
				}
				else
				{
					err = mapper_error::BAD_VALUE;
				}
			}
			break;
		}
		case StandardMetadataType::BLEND_MODE:
		{
			BlendMode blend_mode;
			err = decode_fn(data, data_size, &blend_mode);
			if (err == mapper_error::NONE)
			{
				set_blend_mode(handle, blend_mode);
			}
			break;
		}
		case StandardMetadataType::SMPTE2086:
		{
			std::optional<Smpte2086> smpte2086;
			err = decode_fn(data, data_size, &smpte2086);
			if (err == mapper_error::NONE)
			{
				auto error = set_smpte2086(handle, smpte2086);
				if (error != android::OK)
				{
					err = mapper_error::BAD_VALUE;
				}
			}
			break;
		}
		case StandardMetadataType::CTA861_3:
		{
			std::optional<Cta861_3> cta861_3;
			err = decode_fn(data, data_size, &cta861_3);
			if (err == mapper_error::NONE)
			{
				auto error = set_cta861_3(handle, cta861_3);
				if (error != android::OK)
				{
					err = mapper_error::BAD_VALUE;
				}
			}
			break;
		}
		case StandardMetadataType::SMPTE2094_40:
		{
			std::optional<std::vector<uint8_t>> smpte2094_40;
			err = decode_fn(data, data_size, &smpte2094_40);
			if (err == mapper_error::NONE)
			{
				auto error = set_smpte2094_40(handle, smpte2094_40);
				if (error != android::OK)
				{
					err = mapper_error::BAD_VALUE;
				}
			}
			break;
		}
#if PLATFORM_SDK_VERSION >= 33
		case StandardMetadataType::SMPTE2094_10:
		{
			std::optional<std::vector<uint8_t>> smpte2094_10;
			err = decode_fn(data, data_size, &smpte2094_10);
			if (err == mapper_error::NONE)
			{
				auto error = set_smpte2094_10(handle, smpte2094_10);
				if (error != android::OK)
				{
					err = mapper_error::BAD_VALUE;
				}
			}
			break;
		}
#endif
		case StandardMetadataType::CROP:
		{
			std::vector<Rect> crops;
			err = decode_fn(data, data_size, &crops);
			if (err == mapper_error::NONE)
			{
				auto error = set_crop_rect(handle, crops[0]);
				if (error != android::OK)
				{
					err = mapper_error::BAD_VALUE;
				}
			}
			break;
		}
		/* The following meta data types cannot be changed after allocation. */
		case StandardMetadataType::BUFFER_ID:
		case StandardMetadataType::NAME:
		case StandardMetadataType::WIDTH:
		case StandardMetadataType::HEIGHT:
		case StandardMetadataType::LAYER_COUNT:
		case StandardMetadataType::PIXEL_FORMAT_REQUESTED:
		case StandardMetadataType::USAGE:
			return mapper_error::BAD_VALUE;
		/* Changing other metadata types is unsupported. */
		case StandardMetadataType::PLANE_LAYOUTS:
		case StandardMetadataType::PIXEL_FORMAT_FOURCC:
		case StandardMetadataType::PIXEL_FORMAT_MODIFIER:
		case StandardMetadataType::ALLOCATION_SIZE:
		case StandardMetadataType::PROTECTED_CONTENT:
		case StandardMetadataType::COMPRESSION:
		case StandardMetadataType::INTERLACED:
		case StandardMetadataType::INVALID:
		default:
			return mapper_error::UNSUPPORTED;
		}
		return ((err != mapper_error::NONE) ? mapper_error::UNSUPPORTED : mapper_error::NONE);
	}
#ifdef GRALLOC_AML_EXTEND
	else if (metadata.is_aml_metadata_type())
	{
		android::status_t ret = android::OK;
		android::hardware::hidl_vec<uint8_t> vec{ data, data + data_size };
		const android::hardware::graphics::mapper::V4_0::IMapper::MetadataType
			amlMetadata{ GRALLOC_AML_METADATA_TYPE_NAME, metadata.m_value };
		if (handle->attr_base == MAP_FAILED || handle->attr_base == nullptr)
		{
			MALI_GRALLOC_LOGW("%s: Buffer(%p) valid, may be not imported!", __FUNCTION__, handle);
			return mapper_error::BAD_BUFFER;
		}
		switch (metadata.get_aml_metadata_type_value())
		{
		case AmlMetadataType::AM_OMX_TUNNEL:
		{
			int32_t am_omx_tunnel;
			ret = android::gralloc4::decodeInt32(amlMetadata, vec, &am_omx_tunnel);
			if (!ret)
			{
				set_omx_tunnel(handle, am_omx_tunnel);
			}
			break;
		}
		case AmlMetadataType::AM_OMX_FLAG:
		{
			int32_t am_omx_flag;
			ret = android::gralloc4::decodeInt32(amlMetadata, vec, &am_omx_flag);
			if (!ret)
			{
				set_omx_flag(handle, am_omx_flag);
			}
			break;
		}
		case AmlMetadataType::AM_OMX_VIDEO_TYPE:
		{
			int32_t am_omx_video_type;
			ret = android::gralloc4::decodeInt32(amlMetadata, vec, &am_omx_video_type);
			if (!ret)
			{
				set_omx_video_type(handle, am_omx_video_type);
			}
			break;
		}
		case AmlMetadataType::AM_OMX_BUFFER_SEQUENCE:
		{
			int32_t am_omx_buffer_sequence;
			ret = android::gralloc4::decodeInt32(amlMetadata, vec, &am_omx_buffer_sequence);
			if (!ret)
			{
				set_omx_buffer_sequence(handle, am_omx_buffer_sequence);
			}
			break;
		}
		default:
			ret = android::BAD_VALUE;
		}

		if (ret)
		{
			MALI_GRALLOC_LOGE("set amlogic metadata(%s) error!", metadata.m_name);
			return mapper_error::UNSUPPORTED;
		}
		else
		{
			return mapper_error::NONE;
		}
	}
#endif
	else
	{
		/* None of the vendor types support set. */
		return mapper_error::UNSUPPORTED;
	}
	return mapper_error::NONE;
}

} // namespace common
} // namespace mapper
} // namespace arm
