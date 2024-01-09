/*
 * Copyright (C) 2020-2023 Arm Limited. All rights reserved.
 *
 * Copyright 2016 The Android Open Source Project
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

#include "mapper.h"
#include "aidl/android/hardware/graphics/common/BlendMode.h"
#include "aidl/android/hardware/graphics/common/BufferUsage.h"
#include "aidl/android/hardware/graphics/common/Cta861_3.h"
#include "aidl/android/hardware/graphics/common/Dataspace.h"
#include "aidl/android/hardware/graphics/common/ExtendableType.h"
#include "aidl/android/hardware/graphics/common/PlaneLayout.h"
#include "aidl/android/hardware/graphics/common/Rect.h"
#include "aidl/android/hardware/graphics/common/Smpte2086.h"
#include "aidl/android/hardware/graphics/common/StandardMetadataType.h"
#include "aidl/arm/graphics/ArmMetadataType.h"
#include "aidl/arm/graphics/AmlMetadataType.h"
#include "android/hardware/graphics/mapper/4.0/types.h"
#include "android/rect.h"
#include "hidl/HidlSupport.h"
#include "hidl/Status.h"
#include "idl_common/descriptor.h"
#include "mapper/mapper_hidl.hpp"
#include "mapper/mapper_metadata.h"

#include "allocator/allocator.h"
#include "mapper/mapper_common.hpp"
#include "mapper/mapper_types.hpp"
#include "mapper/mapper_hidl.hpp"
#include "mapper_hidl_header.h"

#include <unordered_map>

namespace arm
{
namespace mapper
{

using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::graphics::mapper::V4_0::BufferDescriptor;
using android::hardware::graphics::mapper::V4_0::Error;
using android::hardware::graphics::mapper::V4_0::IMapper;

using namespace android::gralloc4;
using aidl::android::hardware::graphics::common::BlendMode;
using aidl::android::hardware::graphics::common::Cta861_3;
using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::ExtendableType;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::Rect;
using aidl::android::hardware::graphics::common::Smpte2086;

using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using android::hardware::Void;

static Error mapper_err_to_hidl_err(common::mapper_error error)
{
	switch (error)
	{
	case common::mapper_error::NONE:
		return Error::NONE;
	case common::mapper_error::BAD_DESCRIPTOR:
		return Error::BAD_DESCRIPTOR;
	case common::mapper_error::BAD_BUFFER:
		return Error::BAD_BUFFER;
	case common::mapper_error::BAD_VALUE:
		return Error::BAD_VALUE;
	case common::mapper_error::NO_RESOURCES:
		return Error::NO_RESOURCES;
	case common::mapper_error::UNSUPPORTED:
	default:
		return Error::UNSUPPORTED;
	}
}

GrallocMapper::GrallocMapper()
{
	get_debug_log_level();
}

GrallocMapper::~GrallocMapper()
{
	allocator_close();
}

Return<void> GrallocMapper::createDescriptor(const BufferDescriptorInfo &descriptorInfo, createDescriptor_cb hidl_cb)
{
	if (common::validateDescriptorInfo(descriptorInfo))
	{
		hidl_cb(Error::NONE, common::grallocEncodeBufferDescriptor<uint8_t>(descriptorInfo));
	}
	else
	{
		MALI_GRALLOC_LOGE("Invalid attributes to create descriptor for Mapper 4.0");
		hidl_cb(Error::BAD_VALUE, BufferDescriptor());
	}

	return Void();
}

Return<void> GrallocMapper::importBuffer(const hidl_handle &rawHandle, importBuffer_cb hidl_cb)
{
	imported_handle *imported_handle = nullptr;
	auto err = common::import_buffer(rawHandle.getNativeHandle(), &imported_handle);
	hidl_cb(mapper_err_to_hidl_err(err), imported_handle);

	return Void();
}

Return<Error> GrallocMapper::freeBuffer(void *buffer)
{
	auto err = common::free_buffer(buffer);
	return mapper_err_to_hidl_err(err);
}

/*
 * Retrieves the file descriptor referring to a sync fence object
 *
 * @param fenceHandle [in]  HIDL fence handle
 * @param outFenceFd  [out] Fence file descriptor. '-1' indicates no fence
 *
 * @return false, for an invalid HIDL fence handle
 *         true, otherwise
 */
static bool getFenceFd(const hidl_handle &fenceHandle, int *outFenceFd)
{
	auto const handle = fenceHandle.getNativeHandle();
	if (handle && handle->numFds > 1)
	{
		MALI_GRALLOC_LOGE("Invalid fence handle with %d fds", handle->numFds);
		return false;
	}

	*outFenceFd = (handle && handle->numFds == 1) ? handle->data[0] : -1;
	return true;
}

Return<void> GrallocMapper::lock(void *buffer, uint64_t cpuUsage, const IMapper::Rect &accessRegion,
                                 const hidl_handle &acquireFence, lock_cb hidl_cb)
{
	int fenceFd;
	if (!getFenceFd(acquireFence, &fenceFd))
	{
		hidl_cb(Error::BAD_VALUE, nullptr);
		return Void();
	}

	ARect rect{ accessRegion.left, accessRegion.top, accessRegion.left + accessRegion.width,
		        accessRegion.top + accessRegion.height };

	void *out_data = nullptr;
	auto err = common::lock(buffer, cpuUsage, rect, fenceFd, &out_data);

	hidl_cb(mapper_err_to_hidl_err(err), out_data);
	return Void();
}

/*
 * Populates the HIDL fence handle for the given fence object
 *
 * @param fenceFd       [in] Fence file descriptor
 * @param handleStorage [in] HIDL handle storage for fence
 *
 * @return HIDL fence handle
 */
static hidl_handle getFenceHandle(int fenceFd, char *handleStorage)
{
	native_handle_t *handle = nullptr;
	if (fenceFd >= 0)
	{
		handle = native_handle_init(handleStorage, 1, 0);
		handle->data[0] = fenceFd;
	}

	return hidl_handle(handle);
}

Return<void> GrallocMapper::unlock(void *buffer, unlock_cb hidl_cb)
{
	int release_fence = -1;
	auto err = common::unlock(buffer, release_fence);
	auto hidl_err = mapper_err_to_hidl_err(err);
	if (hidl_err != Error::NONE)
	{
		hidl_cb(hidl_err, nullptr);
		return Void();
	}

	NATIVE_HANDLE_DECLARE_STORAGE(fence_storage, 1, 0);
	hidl_cb(hidl_err, getFenceHandle(release_fence, fence_storage));

	if (release_fence >= 0)
	{
		close(release_fence);
	}
	return Void();
}

Return<void> GrallocMapper::flushLockedBuffer(void *buffer, flushLockedBuffer_cb hidl_cb)
{
	auto err = common::flush_locked_buffer(buffer);
	hidl_cb(mapper_err_to_hidl_err(err), hidl_handle{});
	return Void();
}

Return<Error> GrallocMapper::rereadLockedBuffer(void *buffer)
{
	auto err = common::reread_locked_buffer(buffer);
	return mapper_err_to_hidl_err(err);
}

Return<Error> GrallocMapper::validateBufferSize(void *buffer, const BufferDescriptorInfo &descriptorInfo,
                                                uint32_t in_stride)
{
	/* All Gralloc allocated buffers must be conform to local descriptor validation */
	if (!common::validateDescriptorInfo<BufferDescriptorInfo>(descriptorInfo))
	{
		MALI_GRALLOC_LOGE("Invalid descriptor attributes for validating buffer size");
		MALI_GRALLOC_LOGV("--[%s] %s:%d--\n", __FILE__, __FUNCTION__, __LINE__);
		return Error::BAD_VALUE;
	}
	return hidl::validate_buffer_size(buffer, descriptorInfo, in_stride);
}

/* Encode the number of fds as an int64_t followed by the int64_t fds themselves */
static android::status_t encodeArmPlaneFds_HIDL(const std::vector<int64_t> &fds, hidl_vec<uint8_t> *output)
{
	int64_t n_fds = fds.size();

	output->resize((n_fds + 1) * sizeof(int64_t));

	memcpy(output->data(), &n_fds, sizeof(n_fds));
	memcpy(output->data() + sizeof(n_fds), fds.data(), sizeof(int64_t) * n_fds);

	return android::OK;
}

static android::status_t encodeArmFormatDataType_HIDL(int64_t data_type, hidl_vec<uint8_t> *output)
{
	output->resize(sizeof(int64_t));
	memcpy(output->data(), &data_type, sizeof(int64_t));
	return android::OK;
}

#ifdef GRALLOC_AML_EXTEND
static android::status_t encodeAmlInt32Value_HIDL(int32_t input_value, hidl_vec<uint8_t> *output)
{
	output->resize(sizeof(int32_t));
	memcpy(output->data(), &input_value, sizeof(int32_t));
	return android::OK;
}
common::mapper_error decodeAmlInt32Value_HIDL(const void *data, size_t data_size, void *out)
{
	const auto required_size = sizeof(int32_t);
	if (data_size != required_size)
	{
		MALI_GRALLOC_LOGE("%s:Invalid size found. data_size(%u) != required_size(%u)",
			__FUNCTION__, static_cast<unsigned int>(data_size), static_cast<unsigned int>(required_size));
		return common::mapper_error::BAD_VALUE;
	}

	memcpy(out, &data, required_size);
	return common::mapper_error::NONE;
}
#endif

#define ENCODE_FNC(input_type, encoder)                                                           \
	[](const void *data, std::vector<uint8_t> *output) {                                          \
		hidl_vec<uint8_t> vec;                                                                    \
		auto err = android::gralloc4::encoder(*reinterpret_cast<const input_type *>(data), &vec); \
		if (err == android::OK)                                                                   \
		{                                                                                         \
			*output = std::move(vec);                                                             \
		}                                                                                         \
		return common::android_err_to_mapper_err(err);                                            \
	}
#define ENCODE_FNC_ARM(input_type, encoder)                                    \
	[](const void *data, std::vector<uint8_t> *output) {                       \
		hidl_vec<uint8_t> vec;                                                 \
		auto err = encoder(*reinterpret_cast<const input_type *>(data), &vec); \
		if (err == android::OK)                                                \
		{                                                                      \
			*output = std::move(vec);                                          \
		}                                                                      \
		return common::android_err_to_mapper_err(err);                         \
	}

static std::unordered_map<StandardMetadataType, common::metadata_encoder> standard_encoders = {
	{ StandardMetadataType::BUFFER_ID, ENCODE_FNC(uint64_t, encodeBufferId) },
	{ StandardMetadataType::WIDTH, ENCODE_FNC(uint64_t, encodeWidth) },
	{ StandardMetadataType::HEIGHT, ENCODE_FNC(uint64_t, encodeHeight) },
	{ StandardMetadataType::NAME, ENCODE_FNC(std::string, encodeName) },
	{ StandardMetadataType::LAYER_COUNT, ENCODE_FNC(uint64_t, encodeLayerCount) },
	{ StandardMetadataType::PIXEL_FORMAT_REQUESTED, ENCODE_FNC(PixelFormat, encodePixelFormatRequested) },
	{ StandardMetadataType::PIXEL_FORMAT_FOURCC, ENCODE_FNC(uint32_t, encodePixelFormatFourCC) },
	{ StandardMetadataType::PIXEL_FORMAT_MODIFIER, ENCODE_FNC(uint64_t, encodePixelFormatModifier) },
	{ StandardMetadataType::USAGE, ENCODE_FNC(uint64_t, encodeUsage) },
	{ StandardMetadataType::ALLOCATION_SIZE, ENCODE_FNC(uint64_t, encodeAllocationSize) },
	{ StandardMetadataType::PROTECTED_CONTENT, ENCODE_FNC(uint64_t, encodeProtectedContent) },
	{ StandardMetadataType::COMPRESSION, ENCODE_FNC(ExtendableType, encodeCompression) },
	{ StandardMetadataType::INTERLACED, ENCODE_FNC(ExtendableType, encodeInterlaced) },
	{ StandardMetadataType::CHROMA_SITING, ENCODE_FNC(ExtendableType, encodeChromaSiting) },
	{ StandardMetadataType::PLANE_LAYOUTS, ENCODE_FNC(std::vector<PlaneLayout>, encodePlaneLayouts) },
	{ StandardMetadataType::DATASPACE, ENCODE_FNC(Dataspace, encodeDataspace) },
	{ StandardMetadataType::BLEND_MODE, ENCODE_FNC(BlendMode, encodeBlendMode) },
	{ StandardMetadataType::CROP, ENCODE_FNC(std::vector<Rect>, encodeCrop) },
	{ StandardMetadataType::SMPTE2086, ENCODE_FNC(std::optional<Smpte2086>, encodeSmpte2086) },
	{ StandardMetadataType::CTA861_3, ENCODE_FNC(std::optional<Cta861_3>, encodeCta861_3) },
	{ StandardMetadataType::SMPTE2094_40, ENCODE_FNC(std::optional<std::vector<uint8_t>>, encodeSmpte2094_40) },
	{ StandardMetadataType::SMPTE2094_10, ENCODE_FNC(std::optional<std::vector<uint8_t>>, encodeSmpte2094_10) },
};

static std::unordered_map<ArmMetadataType, common::metadata_encoder> arm_handlers = {
	{ ArmMetadataType::PLANE_FDS, ENCODE_FNC_ARM(std::vector<int64_t>, encodeArmPlaneFds_HIDL) },
	{ ArmMetadataType::FORMAT_DATA_TYPE, ENCODE_FNC_ARM(int64_t, encodeArmFormatDataType_HIDL) },
};

#ifdef GRALLOC_AML_EXTEND
static std::unordered_map<AmlMetadataType, common::metadata_encoder> aml_handlers = {
	{ AmlMetadataType::AM_OMX_TUNNEL, ENCODE_FNC_ARM(int32_t, encodeAmlInt32Value_HIDL) },
	{ AmlMetadataType::AM_OMX_FLAG, ENCODE_FNC_ARM(int32_t, encodeAmlInt32Value_HIDL) },
	{ AmlMetadataType::AM_OMX_VIDEO_TYPE, ENCODE_FNC_ARM(int32_t, encodeAmlInt32Value_HIDL) },
	{ AmlMetadataType::AM_OMX_BUFFER_SEQUENCE, ENCODE_FNC_ARM(int32_t, encodeAmlInt32Value_HIDL) },
};
#endif

static common::metadata_encoder get_encode_function(common::metadata_descriptor &metadata)
{
	/* Empty handler for metadata types we can't handle */
	static common::metadata_encoder empty_handler = [](const void *, void *) {
		return common::mapper_error::BAD_VALUE;
	};

	if (metadata.is_standard_metadata_type())
	{
		auto type = metadata.get_standard_metadata_type_value();
		auto it = standard_encoders.find(type);
		if (it != standard_encoders.end())
		{
			return it->second;
		}
	}
	else if (metadata.is_arm_metadata_type())
	{
		auto type = metadata.get_arm_metadata_type_value();
		auto it = arm_handlers.find(type);
		if (it != arm_handlers.end())
		{
			return it->second;
		}
	}
#ifdef GRALLOC_AML_EXTEND
	else if (metadata.is_aml_metadata_type())
	{
		auto type = metadata.get_aml_metadata_type_value();
		auto it = aml_handlers.find(type);
		if (it != aml_handlers.end())
		{
			return it->second;
		}
	}
#endif

	return empty_handler;
}

Return<void> GrallocMapper::get(void *buffer, const MetadataType &metadataType, IMapper::get_cb hidl_cb)
{
	auto common_metadata = common::metadata_descriptor{ metadataType.name.c_str(), metadataType.value };
	std::vector<uint8_t> vec;

	auto err = common::get(buffer, common_metadata, vec, get_encode_function(common_metadata));
	hidl_cb(mapper_err_to_hidl_err(err), std::move(vec));

	return Void();
}

common::metadata_decoder get_decode_function(const common::metadata_descriptor &metadata)
{
	/* Macro to make use of predefined decode functions provided by the HIDL interface */
#define DECODE_FUNCTION(decode_function, decode_type)                        \
	[](const uint8_t *data, size_t data_size, void *out) {                   \
		hidl_vec<uint8_t> vec{ data, data + data_size };                     \
		auto err = decode_function(vec, reinterpret_cast<decode_type>(out)); \
		if (err != android::OK)                                              \
		{                                                                    \
			return common::mapper_error::BAD_VALUE;                          \
		}                                                                    \
		return common::mapper_error::NONE;                                   \
	}

	static std::unordered_map<StandardMetadataType, common::metadata_decoder> standard_decoders = {
		{ StandardMetadataType::DATASPACE, DECODE_FUNCTION(decodeDataspace, Dataspace *) },
		{ StandardMetadataType::CHROMA_SITING, DECODE_FUNCTION(decodeChromaSiting, ExtendableType *) },
		{ StandardMetadataType::BLEND_MODE, DECODE_FUNCTION(decodeBlendMode, BlendMode *) },
		{ StandardMetadataType::SMPTE2086, DECODE_FUNCTION(decodeSmpte2086, std::optional<Smpte2086> *) },
		{ StandardMetadataType::CTA861_3, DECODE_FUNCTION(decodeCta861_3, std::optional<Cta861_3> *) },
		{ StandardMetadataType::SMPTE2094_40,
		  DECODE_FUNCTION(decodeSmpte2094_40, std::optional<std::vector<uint8_t>> *) },
		{ StandardMetadataType::SMPTE2094_10,
		  DECODE_FUNCTION(decodeSmpte2094_10, std::optional<std::vector<uint8_t>> *) },
		{ StandardMetadataType::CROP, DECODE_FUNCTION(decodeCrop, std::vector<Rect> *) },
	};

#ifdef GRALLOC_AML_EXTEND
		static std::unordered_map<AmlMetadataType, common::metadata_decoder> aml_handlers = {
			{ AmlMetadataType::AM_OMX_TUNNEL, decodeAmlInt32Value_HIDL },
			{ AmlMetadataType::AM_OMX_FLAG, decodeAmlInt32Value_HIDL },
			{ AmlMetadataType::AM_OMX_VIDEO_TYPE, decodeAmlInt32Value_HIDL },
			{ AmlMetadataType::AM_OMX_BUFFER_SEQUENCE, decodeAmlInt32Value_HIDL },
		};
#endif

	auto empty_handler = [](const void *, size_t, void *) { return common::mapper_error::BAD_VALUE; };
	if (metadata.is_standard_metadata_type())
	{
		auto type = metadata.get_standard_metadata_type_value();
		auto it = standard_decoders.find(type);
		if (it != standard_decoders.end())
		{
			return it->second;
		}
	}
#ifdef GRALLOC_AML_EXTEND
	else if (metadata.is_aml_metadata_type())
	{
		auto type = metadata.get_aml_metadata_type_value();
		auto it = aml_handlers.find(type);
		if (it != aml_handlers.end())
		{
			return it->second;
		}
	}
#endif

	return empty_handler;
}

Return<Error> GrallocMapper::set(void *buffer, const MetadataType &metadataType, const hidl_vec<uint8_t> &metadata)
{
	auto common_metadata = common::metadata_descriptor{ metadataType.name.c_str(), metadataType.value };
	auto err =
	    common::set(buffer, common_metadata, metadata.data(), metadata.size(), get_decode_function(common_metadata));

	return mapper_err_to_hidl_err(err);
}

Return<void> GrallocMapper::getFromBufferDescriptorInfo(const BufferDescriptorInfo &description,
                                                        const MetadataType &metadataType,
                                                        getFromBufferDescriptorInfo_cb hidl_cb)
{
	hidl::get_from_buffer_descriptor_info(description, metadataType, hidl_cb);
	return Void();
}

Return<void> GrallocMapper::getTransportSize(void *buffer, getTransportSize_cb hidl_cb)
{
	int num_fds = -1;
	int num_ints = -1;
	auto err = common::get_transport_size(buffer, num_fds, num_ints);
	hidl_cb(mapper_err_to_hidl_err(err), num_fds, num_ints);

	return Void();
}

Return<void> GrallocMapper::isSupported(const IMapper::BufferDescriptorInfo &description, isSupported_cb hidl_cb)
{
	if (!common::validateDescriptorInfo<BufferDescriptorInfo>(description))
	{
		MALI_GRALLOC_LOGE("Invalid descriptor attributes for validating buffer size");
		MALI_GRALLOC_LOGV("--[%s] %s:%d--\n", __FILE__, __FUNCTION__, __LINE__);
		hidl_cb(Error::BAD_VALUE, false);
	}

	hidl::is_supported(description, hidl_cb);
	return Void();
}

static GrallocMapper::MetadataTypeDescription common_metadata_type_description_to_hidl(
    const common::metadata_type &metadata)
{
	return GrallocMapper::MetadataTypeDescription{
		{ metadata.m_descriptor.m_name, metadata.m_descriptor.m_value },
		metadata.m_description,
		metadata.m_is_gettable,
		metadata.m_is_settable,
	};
}

Return<void> GrallocMapper::listSupportedMetadataTypes(listSupportedMetadataTypes_cb hidl_cb)
{
	auto &supported_types = common::list_supported_metadata_types();

	std::vector<GrallocMapper::MetadataTypeDescription> hidl_metadata;
	hidl_metadata.reserve(supported_types.size());
	for (auto &metadata : supported_types)
	{
		hidl_metadata.push_back(common_metadata_type_description_to_hidl(metadata));
	}

	hidl_cb(Error::NONE, hidl_metadata);
	return Void();
}

static IMapper::MetadataType common_metadata_type_to_hidl(const common::metadata_descriptor &type)
{
	return IMapper::MetadataType{ type.m_name, type.m_value };
}

static IMapper::BufferDump common_buffer_dump_to_hidl(const common::buffer_dump &buf_dump)
{
	std::vector<IMapper::MetadataDump> buf{};
	for (auto &it : buf_dump.metadata)
	{
		buf.push_back({ common_metadata_type_to_hidl(it.type), it.data });
	}
	return { std::move(buf) };
}

Return<void> GrallocMapper::dumpBuffer(void *buffer, dumpBuffer_cb hidl_cb)
{
	common::buffer_dump buffer_dump;
	auto err = common::dump_buffer(buffer, buffer_dump, standard_encoders, false);
	auto hidl_err = mapper_err_to_hidl_err(err);

	IMapper::BufferDump hidl_buf_dump{};
	if (hidl_err == Error::NONE)
	{
		hidl_buf_dump = common_buffer_dump_to_hidl(buffer_dump);
	}

	hidl_cb(hidl_err, hidl_buf_dump);
	return Void();
}

Return<void> GrallocMapper::dumpBuffers(dumpBuffers_cb hidl_cb)
{
	std::vector<common::buffer_dump> buffer_dumps;
	auto err = common::dump_buffers(buffer_dumps, standard_encoders, false);
	auto hidl_err = mapper_err_to_hidl_err(err);

	std::vector<IMapper::BufferDump> hidl_buffer_dumps;
	if (hidl_err == Error::NONE)
	{
		for (auto &it : buffer_dumps)
		{
			auto hidl_buf_dump = common_buffer_dump_to_hidl(it);
			hidl_buffer_dumps.push_back(hidl_buf_dump);
		}
	}

	hidl_cb(mapper_err_to_hidl_err(err), hidl_buffer_dumps);
	return Void();
}

Return<void> GrallocMapper::getReservedRegion(void *buffer, getReservedRegion_cb hidl_cb)
{
	void *reserved_region_address = nullptr;
	uint64_t reserved_region_size = 0;

	auto err = common::get_reserved_region(buffer, &reserved_region_address, reserved_region_size);
	hidl_cb(mapper_err_to_hidl_err(err), reserved_region_address, reserved_region_size);

	return Void();
}
} // namespace mapper
} // namespace arm

extern "C" IMapper *HIDL_FETCH_IMapper(const char * /* name */)
{
	//open the log will cause media.swcodec crash
	//MALI_GRALLOC_LOGV("Arm Module IMapper %d, pid = %d ppid = %d ", GRALLOC_MAPPER_VERSION_MAJOR, getpid(), getppid());

	return new arm::mapper::GrallocMapper();
}
