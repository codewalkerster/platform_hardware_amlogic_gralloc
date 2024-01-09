/*
 * Copyright (C) 2023 Arm Limited.
 * SPDX-License-Identifier: Apache-2.0
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
#include "mapper/mapper_hidl.hpp"
#include "idl_common/descriptor.h"
#include "core/buffer_allocation.h"
#include "idl_common/shared_metadata.h"
#include "core/format_info.h"
#include "core/drm_utils.h"
#include "core/buffer.h"
#include "log.h"
#include "gralloctypes/Gralloc4.h"
#include "usages.h"

namespace arm
{
namespace mapper
{
namespace hidl
{

using aidl::android::hardware::graphics::common::BlendMode;
using aidl::android::hardware::graphics::common::Cta861_3;
using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::ExtendableType;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::Rect;
using aidl::android::hardware::graphics::common::Smpte2086;
using aidl::android::hardware::graphics::common::StandardMetadataType;
using aidl::arm::graphics::ArmMetadataType;
using aidl::arm::graphics::AmlMetadataType;
using android::hardware::hidl_vec;

using MetadataType = android::hardware::graphics::mapper::V4_0::IMapper::MetadataType;

void is_supported(const IMapper::BufferDescriptorInfo &description, IMapper::isSupported_cb hidl_cb)
{
	buffer_descriptor_t grallocDescriptor;
	grallocDescriptor.width = description.width;
	grallocDescriptor.height = description.height;
	grallocDescriptor.layer_count = description.layerCount;
	grallocDescriptor.hal_format = static_cast<uint64_t>(description.format);
	grallocDescriptor.producer_usage = static_cast<uint64_t>(description.usage);
	grallocDescriptor.consumer_usage = grallocDescriptor.producer_usage;

	grallocDescriptor.flags |= common::DESCRIPTOR_ALLOCATOR_FLAGS;

	/* Check if it is possible to allocate a buffer for the given description */
	const int result = mali_gralloc_derive_format_and_size(&grallocDescriptor);
	if (result != 0)
	{
		MALI_GRALLOC_LOGV("Allocation for the given description will not succeed. error: %d", result);
	}
	hidl_cb(Error::NONE, result == 0);
}

static bool isArmMetadataType(const MetadataType &metadataType)
{
	return metadataType.name == GRALLOC_ARM_METADATA_TYPE_NAME;
}

static bool isAmlMetadataType(const MetadataType &metadataType)
{
	return metadataType.name == GRALLOC_AML_METADATA_TYPE_NAME;
}

static ArmMetadataType getArmMetadataTypeValue(const MetadataType &metadataType)
{
	return static_cast<ArmMetadataType>(metadataType.value);
}

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
	case mali_gralloc_format_data_type::SFLOAT:
		return (aidl::arm::graphics::DataType::SFLOAT);
		break;
	default:
		return (aidl::arm::graphics::DataType::UNKNOWN);
		break;
	}
}

void get_from_buffer_descriptor_info(IMapper::BufferDescriptorInfo const &description,
                                     IMapper::MetadataType const &metadataType,
                                     IMapper::getFromBufferDescriptorInfo_cb hidl_cb)
{
	/* This will hold the metadata that is returned. */
	hidl_vec<uint8_t> vec;

	buffer_descriptor_t descriptor{};
	descriptor.width = description.width;
	descriptor.height = description.height;
	descriptor.layer_count = description.layerCount;
	descriptor.hal_format = static_cast<uint64_t>(description.format);
	descriptor.producer_usage = static_cast<uint64_t>(description.usage);
	descriptor.consumer_usage = descriptor.producer_usage;

	descriptor.flags = common::DESCRIPTOR_ALLOCATOR_FLAGS;

	/* Check if it is possible to allocate a buffer for the given description */
	const int alloc_result = mali_gralloc_derive_format_and_size(&descriptor);
	if (alloc_result != 0)
	{
		MALI_GRALLOC_LOGV("Allocation for the given description will not succeed. error: %d", alloc_result);
		hidl_cb(Error::BAD_VALUE, vec);
		return;
	}

	const auto *format_info = descriptor.alloc_format.get_base_info();

	/* Create buffer handle from the initialized descriptor without a backing store or shared metadata region.
	 * Used to share functionality with the normal metadata get function that can only use the allocated buffer handle
	 * and does not have the buffer descriptor available. */
	private_handle_t partial_handle(0, descriptor.size, descriptor.consumer_usage, descriptor.producer_usage, -1,
	                                descriptor.hal_format, descriptor.alloc_format, descriptor.width, descriptor.height,
	                                descriptor.layer_count, descriptor.plane_info, descriptor.pixel_stride,
	                                ((format_info && format_info->npln > 1) ? true : false));
	if (android::gralloc4::isStandardMetadataType(metadataType))
	{
		android::status_t err = android::OK;

		switch (android::gralloc4::getStandardMetadataTypeValue(metadataType))
		{
		case StandardMetadataType::NAME:
			err = android::gralloc4::encodeName(description.name, &vec);
			break;
		case StandardMetadataType::WIDTH:
			err = android::gralloc4::encodeWidth(description.width, &vec);
			break;
		case StandardMetadataType::HEIGHT:
			err = android::gralloc4::encodeHeight(description.height, &vec);
			break;
		case StandardMetadataType::LAYER_COUNT:
			err = android::gralloc4::encodeLayerCount(description.layerCount, &vec);
			break;
		case StandardMetadataType::PIXEL_FORMAT_REQUESTED:
			err = android::gralloc4::encodePixelFormatRequested(static_cast<PixelFormat>(description.format), &vec);
			break;
		case StandardMetadataType::USAGE:
			err = android::gralloc4::encodeUsage(description.usage, &vec);
			break;
		case StandardMetadataType::PIXEL_FORMAT_FOURCC:
			err = android::gralloc4::encodePixelFormatFourCC(drm_fourcc_from_handle(&partial_handle), &vec);
			break;
		case StandardMetadataType::PIXEL_FORMAT_MODIFIER:
			err = android::gralloc4::encodePixelFormatModifier(drm_modifier_from_handle(&partial_handle), &vec);
			break;
		case StandardMetadataType::ALLOCATION_SIZE:
			err = android::gralloc4::encodeAllocationSize(partial_handle.size, &vec);
			break;
		case StandardMetadataType::PROTECTED_CONTENT:
		{
			/* This is set to 1 if the buffer has protected content. */
			const int is_protected =
			    (((partial_handle.consumer_usage | partial_handle.producer_usage) & BufferUsage::PROTECTED)) ? 1 : 0;
			err = android::gralloc4::encodeProtectedContent(is_protected, &vec);
			break;
		}
		case StandardMetadataType::COMPRESSION:
		{
			ExtendableType compression;
			const auto internal_format = partial_handle.alloc_format;
			if (internal_format.is_afbc())
			{
				compression = common::Compression_AFBC;
			}
			else if (internal_format.is_afrc())
			{
				compression = common::Compression_AFRC;
			}
			else
			{
				compression = android::gralloc4::Compression_None;
			}
			err = android::gralloc4::encodeCompression(compression, &vec);
			break;
		}
		case StandardMetadataType::INTERLACED:
			err = android::gralloc4::encodeInterlaced(android::gralloc4::Interlaced_None, &vec);
			break;
		case StandardMetadataType::CHROMA_SITING:
		{
			const auto *format_info = partial_handle.alloc_format.get_base_info();
			if (format_info == nullptr)
			{
				err = android::BAD_VALUE;
				break;
			}

			ExtendableType chroma_siting = android::gralloc4::ChromaSiting_None;
			if (format_info->is_yuv)
			{
				chroma_siting = android::gralloc4::ChromaSiting_Unknown;
			}
			err = android::gralloc4::encodeChromaSiting(chroma_siting, &vec);
			break;
		}
		case StandardMetadataType::PLANE_LAYOUTS:
		{
			std::vector<PlaneLayout> layouts;
			auto mapper_error = common::get_plane_layouts(&partial_handle, &layouts);
			if (mapper_error == common::mapper_error::NONE)
			{
				err = android::gralloc4::encodePlaneLayouts(layouts, &vec);
			}
			else
			{
				err = android::BAD_VALUE;
			}
			break;
		}
		case StandardMetadataType::DATASPACE:
		{
			android_dataspace_t dataspace;
			get_format_dataspace(partial_handle.alloc_format.get_base_info(),
			                     partial_handle.consumer_usage | partial_handle.producer_usage, &dataspace);
			err = android::gralloc4::encodeDataspace(static_cast<Dataspace>(dataspace), &vec);
			break;
		}
		case StandardMetadataType::BLEND_MODE:
			err = android::gralloc4::encodeBlendMode(BlendMode::INVALID, &vec);
			break;
		case StandardMetadataType::CROP:
		{
			const int num_planes = partial_handle.get_num_planes();
			std::vector<Rect> crops(num_planes);
			for (size_t plane_index = 0; plane_index < num_planes; ++plane_index)
			{
				Rect rect = { .left = 0,
					          .top = 0,
					          .right = static_cast<int32_t>(partial_handle.plane_info[plane_index].alloc_width),
					          .bottom = static_cast<int32_t>(partial_handle.plane_info[plane_index].alloc_height) };
				if (plane_index == 0)
				{
					rect = { .left = 0, .top = 0, .right = partial_handle.width, .bottom = partial_handle.height };
				}
				crops[plane_index] = rect;
			}
			err = android::gralloc4::encodeCrop(crops, &vec);
			break;
		}
		case StandardMetadataType::SMPTE2086:
		{
			std::optional<Smpte2086> smpte2086{};
			err = android::gralloc4::encodeSmpte2086(smpte2086, &vec);
			break;
		}
		case StandardMetadataType::CTA861_3:
		{
			std::optional<Cta861_3> cta861_3{};
			err = android::gralloc4::encodeCta861_3(cta861_3, &vec);
			break;
		}
		case StandardMetadataType::SMPTE2094_40:
		{
			std::optional<std::vector<uint8_t>> smpte2094_40{};
			err = android::gralloc4::encodeSmpte2094_40(smpte2094_40, &vec);
			break;
		}
		case StandardMetadataType::SMPTE2094_10:
		{
			std::optional<std::vector<uint8_t>> smpte2094_10{};
			err = android::gralloc4::encodeSmpte2094_10(smpte2094_10, &vec);
			break;
		}
		case StandardMetadataType::BUFFER_ID:
		case StandardMetadataType::INVALID:
		default:
			err = android::BAD_VALUE;
		}
		hidl_cb((err) ? Error::UNSUPPORTED : Error::NONE, vec);
	}
	else if (isArmMetadataType(metadataType))
	{
		android::status_t err = android::OK;

		switch (getArmMetadataTypeValue(metadataType))
		{
		case ArmMetadataType::FORMAT_DATA_TYPE:
		{
			int64_t data_type =
			    static_cast<int64_t>(data_type_internal_to_aidl(partial_handle.alloc_format.get_format_data_type()));
			vec.resize(sizeof(int64_t));
			memcpy(vec.data(), &data_type, sizeof(int64_t));
			break;
		}
		default:
			err = android::BAD_VALUE;
		}
		hidl_cb((err) ? Error::UNSUPPORTED : Error::NONE, vec);
	}
#ifdef GRALLOC_AML_EXTEND
	else if (isAmlMetadataType(metadataType))
	{
		// TODO: This function is not called by other functions and is not implemented yet
		MALI_GRALLOC_LOGW("!!! not support currently!");
		hidl_cb(Error::UNSUPPORTED, vec);
	}
#endif
	else
	{
		hidl_cb(Error::UNSUPPORTED, vec);
	}
}

Error validate_buffer_size(void *buffer, const IMapper::BufferDescriptorInfo &descriptorInfo, uint32_t in_stride)
{
	auto handle = handle_cast<imported_handle>(static_cast<native_handle *>(buffer));
	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("validateBufferSize: %p has not been imported", buffer);
		return Error::BAD_BUFFER;
	}

	/* Validate the buffer parameters against descriptor info */

	/* The descriptor dimensions must match the buffer */
	if (static_cast<uint32_t>(handle->width) != descriptorInfo.width)
	{
		MALI_GRALLOC_LOGE("Width mismatch. Buffer width = %u, Descriptor width = %u", handle->width,
		                  descriptorInfo.width);
		return Error::BAD_VALUE;
	}

	if (static_cast<uint32_t>(handle->height) != descriptorInfo.height)
	{
		MALI_GRALLOC_LOGE("Height mismatch. Buffer height = %u, Descriptor height = %u", handle->height,
		                  descriptorInfo.height);
		return Error::BAD_VALUE;
	}

	if (handle->layer_count != descriptorInfo.layerCount)
	{
		MALI_GRALLOC_LOGE("Layer Count mismatch. Buffer layer_count = %u, Descriptor layer_count = %u",
		                  handle->layer_count, descriptorInfo.layerCount);
		return Error::BAD_VALUE;
	}

	/* Some usages need to match and the rest of the usage must be a subset of the buffer's usages */
	uint64_t must_match_mask = GRALLOC_USAGE_PRIVATE_MASK | GRALLOC_USAGE_PROTECTED;
	uint64_t descriptor_usage = static_cast<uint64_t>(descriptorInfo.usage) & ~(GRALLOC_USAGE_EXTERNAL_DISP);
	uint64_t buffer_usage = handle->producer_usage | handle->consumer_usage;

	if ((buffer_usage & descriptor_usage) != descriptor_usage)
	{
		MALI_GRALLOC_LOGE("Usage not a subset. Buffer usage = %#" PRIx64 ", Descriptor usage = %#" PRIx64, buffer_usage,
		                  descriptor_usage);
		return Error::BAD_VALUE;
	}

	if ((buffer_usage & must_match_mask) != (descriptor_usage & must_match_mask))
	{
		MALI_GRALLOC_LOGE("Usage mismatch. Buffer usage = %#" PRIx64 ", Descriptor usage = %#" PRIx64, buffer_usage,
		                  descriptor_usage);
		return Error::BAD_VALUE;
	}

	/* The stride used should match the stride returned on buffer allocation. */
	if (in_stride != 0 && static_cast<uint32_t>(handle->stride) != in_stride)
	{
		MALI_GRALLOC_LOGE("Stride mismatch. Expected stride = %d, Buffer stride = %d", in_stride, handle->stride);
		return Error::BAD_VALUE;
	}

	/* The requested format must match. It may be possible for some formats to be compatible but there are no compelling
	 * use cases for a more complex check.
	 */
	int descriptor_format = static_cast<int>(descriptorInfo.format);
	if (handle->req_format != descriptor_format)
	{
		MALI_GRALLOC_LOGE("Buffer requested format: %#x does not match descriptor format: %#x", handle->req_format,
		                  descriptor_format);
		return Error::BAD_VALUE;
	}

	return Error::NONE;
}

} // namespace hidl
} // namespace mapper
} // namespace arm
