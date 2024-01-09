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

#include "mapper.hpp"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <android-base/unique_fd.h>

#include "aidl/android/hardware/graphics/common/BufferUsage.h"
#include "aidl/android/hardware/graphics/common/PixelFormat.h"
#include "aidl/arm/graphics/ArmMetadataType.h"
#include "aidl/arm/graphics/AmlMetadataType.h"
#include "android/hardware/graphics/mapper/IMapper.h"
#include "core/buffer.h"
#include "mapper/mapper_common.hpp"
#include "mapper/mapper_metadata.h"
#include "mapper/mapper_types.hpp"

using namespace ::android::hardware::graphics::mapper;
using namespace arm::mapper;

using aidl::android::hardware::graphics::common::BlendMode;
using aidl::android::hardware::graphics::common::BufferUsage;
using aidl::android::hardware::graphics::common::Cta861_3;
using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::ExtendableType;
using aidl::android::hardware::graphics::common::PixelFormat;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::Rect;
using aidl::android::hardware::graphics::common::Smpte2086;
using aidl::arm::graphics::ArmMetadataType;
using aidl::arm::graphics::AmlMetadataType;
using common::mapper_data;

static AIMapper_Error mapper_error_to_stablec_error(common::mapper_error error)
{
	switch (error)
	{
	case common::mapper_error::NONE:
		return AIMAPPER_ERROR_NONE;
	case common::mapper_error::BAD_DESCRIPTOR:
		return AIMAPPER_ERROR_BAD_DESCRIPTOR;
	case common::mapper_error::BAD_BUFFER:
		return AIMAPPER_ERROR_BAD_BUFFER;
	case common::mapper_error::BAD_VALUE:
		return AIMAPPER_ERROR_BAD_VALUE;
	case common::mapper_error::NO_RESOURCES:
		return AIMAPPER_ERROR_NO_RESOURCES;
	case common::mapper_error::UNSUPPORTED:
	default:
		return AIMAPPER_ERROR_UNSUPPORTED;
	}
}

static common::mapper_error stablec_error_to_mapper_error(AIMapper_Error stablec_error)
{
	switch (stablec_error)
	{
	case AIMapper_Error::AIMAPPER_ERROR_NONE:
		return common::mapper_error::NONE;
	case AIMapper_Error::AIMAPPER_ERROR_BAD_BUFFER:
		return common::mapper_error::BAD_BUFFER;
	case AIMapper_Error::AIMAPPER_ERROR_BAD_VALUE:
		return common::mapper_error::BAD_VALUE;
	case AIMapper_Error::AIMAPPER_ERROR_NO_RESOURCES:
		return common::mapper_error::NO_RESOURCES;
	default:
		return common::mapper_error::UNSUPPORTED;
	}
}

GrallocMapperV5::GrallocMapperV5()
{
	MALI_GRALLOC_LOGV("Arm Module IMapper %d loaded, pid = %d ppid = %d", 5, getpid(), getppid());

	auto &supported_types = common::list_supported_metadata_types();
	for (auto &metadata : supported_types)
	{
		auto type = AIMapper_MetadataType{ metadata.m_descriptor.m_name, metadata.m_descriptor.m_value };
		m_stablec_metadata.push_back(
		    { type, metadata.m_description, metadata.m_is_gettable, metadata.m_is_settable, {} });
	}
	get_debug_log_level();
}

AIMapper_Error GrallocMapperV5::importBuffer(const native_handle_t *_Nonnull handle,
	buffer_handle_t _Nullable *_Nonnull outBufferHandle)
{
	imported_handle *imported_handle;
	auto err = common::import_buffer(handle, &imported_handle);

	auto mapper_error = mapper_error_to_stablec_error(err);
	if (mapper_error != AIMAPPER_ERROR_NONE)
	{
		return mapper_error;
	}

	*outBufferHandle = imported_handle;
	return mapper_error;
}

AIMapper_Error GrallocMapperV5::freeBuffer(buffer_handle_t _Nonnull buffer)
{
	auto err = common::free_buffer(const_cast<native_handle_t *>(buffer));
	return mapper_error_to_stablec_error(err);
}

AIMapper_Error GrallocMapperV5::lock(buffer_handle_t _Nonnull buffer, uint64_t cpuUsage, ARect accessRegion,
	int acquireFence, void *_Nullable *_Nonnull outData)
{
	android::base::unique_fd owned_acquire_fence{ acquireFence };
	auto err = common::lock(buffer, cpuUsage, accessRegion, owned_acquire_fence.get(), outData);
	return mapper_error_to_stablec_error(err);
}

AIMapper_Error GrallocMapperV5::unlock(buffer_handle_t _Nonnull buffer, int *_Nonnull releaseFence)
{
	auto err = common::unlock(buffer, *releaseFence);
	return mapper_error_to_stablec_error(err);
}

AIMapper_Error GrallocMapperV5::flushLockedBuffer(buffer_handle_t _Nonnull buffer)
{
	auto err = common::flush_locked_buffer(buffer);
	return mapper_error_to_stablec_error(err);
}

AIMapper_Error GrallocMapperV5::rereadLockedBuffer(buffer_handle_t _Nonnull buffer)
{
	auto err = common::reread_locked_buffer(buffer);
	return mapper_error_to_stablec_error(err);
}

AIMapper_Error GrallocMapperV5::getReservedRegion(buffer_handle_t _Nonnull buffer,
void *_Nullable *_Nonnull outReservedRegion, uint64_t *_Nonnull outReservedSize)
{
	auto err = common::get_reserved_region(buffer, outReservedRegion, *outReservedSize);
	return mapper_error_to_stablec_error(err);
}

/* Encode the number of fds as an int64_t followed by the int64_t fds themselves */
static int32_t encodeArmPlaneFds_stableC(const std::vector<int64_t> &fds, void *out_data, size_t out_data_size)
{
	const int64_t n_fds = fds.size();
	const auto required_size = (n_fds + 1) * sizeof(int64_t);
	if (out_data_size < required_size)
	{
		return required_size;
	}

	memcpy(out_data, &n_fds, sizeof(n_fds));
	memcpy(reinterpret_cast<std::byte *>(out_data) + sizeof(n_fds), fds.data(), sizeof(int64_t) * n_fds);

	return required_size;
}

static int32_t encodeArmFormatDataType_stableC(int64_t data_type, void *out_data, size_t out_data_size)
{
	const auto required_size = sizeof(int64_t);
	if (out_data_size < required_size)
	{
		return required_size;
	}

	memcpy(out_data, &data_type, sizeof(int64_t));
	return required_size;
}
#ifdef GRALLOC_AML_EXTEND
static int32_t encodeAmlInt32Value_stableC(int32_t input_value, void *out_data, size_t out_data_size)
{
	const auto required_size = sizeof(int32_t);
	if (out_data_size < required_size)
	{
		return required_size;
	}

	memcpy(out_data, &input_value, sizeof(int32_t));
	return required_size;
}
common::mapper_error decodeAmlInt32Value_stableC(const void *data, size_t data_size, int32_t *out)
{
	const auto required_size = sizeof(int32_t);
	if (data_size != required_size)
	{
		return common::mapper_error::NO_RESOURCES;
	}

	memcpy(out, &data, required_size);
	return common::mapper_error::NONE;
}
#endif

static common::mapper_error process_encoder_result(mapper_data &data, int32_t encoder_result)
{
	data.result = encoder_result;
	if (encoder_result < 0)
	{
		return stablec_error_to_mapper_error(static_cast<AIMapper_Error>(-encoder_result));
	}
	else if (encoder_result > data.out_data_size)
	{
		return common::mapper_error::NO_RESOURCES;
	}
	return common::mapper_error::NONE;
}

#define ENCODE_FNC(input_type, encoder)                                                                \
	[](const void *data, std::vector<uint8_t> *output) {                                               \
		auto *out_data = reinterpret_cast<mapper_data *>(output->data());                              \
		int32_t err = StandardMetadata<StandardMetadataType::encoder>::value::encode(                  \
		    *reinterpret_cast<const input_type *>(data), out_data->out_data, out_data->out_data_size); \
		return process_encoder_result(*out_data, err);                                                 \
	}

#define ENCODE_FNC_ARM(input_type, encoder)                                                                    \
	[](const void *data, std::vector<uint8_t> *output) {                                                       \
		auto *out_data = reinterpret_cast<mapper_data *>(output->data());                                      \
		int32_t err =                                                                                          \
		    encoder(*reinterpret_cast<const input_type *>(data), out_data->out_data, out_data->out_data_size); \
		return process_encoder_result(*out_data, err);                                                         \
	}

static std::unordered_map<StandardMetadataType, common::metadata_encoder> standard_handlers = {
	{ StandardMetadataType::BUFFER_ID, ENCODE_FNC(uint64_t, BUFFER_ID) },
	{ StandardMetadataType::WIDTH, ENCODE_FNC(uint64_t, WIDTH) },
	{ StandardMetadataType::HEIGHT, ENCODE_FNC(uint64_t, HEIGHT) },
	{ StandardMetadataType::NAME, ENCODE_FNC(std::string, NAME) },
	{ StandardMetadataType::LAYER_COUNT, ENCODE_FNC(uint64_t, LAYER_COUNT) },
	{ StandardMetadataType::PIXEL_FORMAT_REQUESTED, ENCODE_FNC(PixelFormat, PIXEL_FORMAT_REQUESTED) },
	{ StandardMetadataType::PIXEL_FORMAT_FOURCC, ENCODE_FNC(uint32_t, PIXEL_FORMAT_FOURCC) },
	{ StandardMetadataType::PIXEL_FORMAT_MODIFIER, ENCODE_FNC(uint64_t, PIXEL_FORMAT_MODIFIER) },
	/* Note: buffer usage is represented as a unsigned integer in Gralloc, AIDL BufferUsage is a signed integer */
	{ StandardMetadataType::USAGE, ENCODE_FNC(BufferUsage, USAGE) },
	{ StandardMetadataType::ALLOCATION_SIZE, ENCODE_FNC(uint64_t, ALLOCATION_SIZE) },
	{ StandardMetadataType::PROTECTED_CONTENT, ENCODE_FNC(uint64_t, PROTECTED_CONTENT) },
	{ StandardMetadataType::COMPRESSION, ENCODE_FNC(ExtendableType, COMPRESSION) },
	{ StandardMetadataType::INTERLACED, ENCODE_FNC(ExtendableType, INTERLACED) },
	{ StandardMetadataType::CHROMA_SITING, ENCODE_FNC(ExtendableType, CHROMA_SITING) },
	{ StandardMetadataType::PLANE_LAYOUTS, ENCODE_FNC(std::vector<PlaneLayout>, PLANE_LAYOUTS) },
	{ StandardMetadataType::DATASPACE, ENCODE_FNC(Dataspace, DATASPACE) },
	{ StandardMetadataType::BLEND_MODE, ENCODE_FNC(BlendMode, BLEND_MODE) },
	{ StandardMetadataType::CROP, ENCODE_FNC(std::vector<Rect>, CROP) },
	{ StandardMetadataType::SMPTE2086, ENCODE_FNC(std::optional<Smpte2086>, SMPTE2086) },
	{ StandardMetadataType::CTA861_3, ENCODE_FNC(std::optional<Cta861_3>, CTA861_3) },
	{ StandardMetadataType::SMPTE2094_40, ENCODE_FNC(std::optional<std::vector<uint8_t>>, SMPTE2094_40) },
	{ StandardMetadataType::SMPTE2094_10, ENCODE_FNC(std::optional<std::vector<uint8_t>>, SMPTE2094_10) },
	{ StandardMetadataType::STRIDE, ENCODE_FNC(uint32_t, STRIDE) },
};

static std::unordered_map<ArmMetadataType, common::metadata_encoder> arm_handlers = {
	{ ArmMetadataType::PLANE_FDS, ENCODE_FNC_ARM(std::vector<int64_t>, encodeArmPlaneFds_stableC) },
	{ ArmMetadataType::FORMAT_DATA_TYPE, ENCODE_FNC_ARM(int64_t, encodeArmFormatDataType_stableC) },
};
#ifdef GRALLOC_AML_EXTEND
static std::unordered_map<AmlMetadataType, common::metadata_encoder> aml_handlers = {
	{ AmlMetadataType::AM_OMX_TUNNEL, ENCODE_FNC_ARM(int32_t, encodeAmlInt32Value_stableC) },
	{ AmlMetadataType::AM_OMX_FLAG, ENCODE_FNC_ARM(int32_t, encodeAmlInt32Value_stableC) },
	{ AmlMetadataType::AM_OMX_VIDEO_TYPE, ENCODE_FNC_ARM(int32_t, encodeAmlInt32Value_stableC) },
	{ AmlMetadataType::AM_OMX_BUFFER_SEQUENCE, ENCODE_FNC_ARM(int32_t, encodeAmlInt32Value_stableC) },
};
#endif

static common::metadata_encoder get_encode_function(common::metadata_descriptor &metadata)
{
	/* Empty handler for metadata types we can't handle */
	static common::metadata_encoder empty_handler = [](const void *, std::vector<uint8_t> *) {
		return common::mapper_error::UNSUPPORTED;
	};

	if (metadata.is_standard_metadata_type())
	{
		auto type = metadata.get_standard_metadata_type_value();
		auto it = standard_handlers.find(type);
		if (it != standard_handlers.end())
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

int32_t GrallocMapperV5::getMetadata(buffer_handle_t _Nonnull buffer, AIMapper_MetadataType metadataType,
	void *_Nonnull outData, size_t outDataSize)
{
	auto common_metadata = common::metadata_descriptor{ metadataType.name, metadataType.value };

	std::vector<uint8_t> out_buffer;
	out_buffer.resize(sizeof(mapper_data));

	mapper_data *data = reinterpret_cast<mapper_data *>(out_buffer.data());
	*data = { outData, outDataSize, 0 };

	auto err = common::get(buffer, common_metadata, out_buffer, get_encode_function(common_metadata));
	if (err != common::mapper_error::NONE && err != common::mapper_error::NO_RESOURCES)
	{
		ALOGE("Failed to get metadata. Mapper error: %d\n", static_cast<int>(err));
		return -mapper_error_to_stablec_error(err);
	}

	return data->result;
}

int32_t GrallocMapperV5::getStandardMetadata(buffer_handle_t _Nonnull buffer, int64_t standardMetadataType,
	void *_Nonnull outData, size_t outDataSize)
{
	AIMapper_MetadataType standard_metadata{ common::STANDARD_METADATA_NAME, standardMetadataType };
	return getMetadata(buffer, standard_metadata, outData, outDataSize);
}

common::metadata_decoder get_decode_function(const common::metadata_descriptor &metadata)
{
	/* Macro to make use of predefined decode functions provided by the stableC interface */
#define DECODE_FUNCTION(standard_metadata_type)                                                                      \
	[](const void *data, size_t data_size, void *out) {                                                              \
		auto value = StandardMetadata<StandardMetadataType::standard_metadata_type>::value::decode(data, data_size); \
		if (!value.has_value())                                                                                      \
		{                                                                                                            \
			return common::mapper_error::BAD_VALUE;                                                                  \
		}                                                                                                            \
                                                                                                                     \
		using value_type = decltype(&*value);                                                                        \
		*reinterpret_cast<value_type>(out) = *value;                                                                 \
                                                                                                                     \
		return common::mapper_error::NONE;                                                                           \
	}

	static std::unordered_map<StandardMetadataType, common::metadata_decoder> standard_handlers = {
		{ StandardMetadataType::DATASPACE, DECODE_FUNCTION(DATASPACE) },
		{ StandardMetadataType::CHROMA_SITING, DECODE_FUNCTION(CHROMA_SITING) },
		{ StandardMetadataType::BLEND_MODE, DECODE_FUNCTION(BLEND_MODE) },
		{ StandardMetadataType::SMPTE2086, DECODE_FUNCTION(SMPTE2086) },
		{ StandardMetadataType::CTA861_3, DECODE_FUNCTION(CTA861_3) },
		{ StandardMetadataType::SMPTE2094_40, DECODE_FUNCTION(SMPTE2094_40) },
		{ StandardMetadataType::SMPTE2094_10, DECODE_FUNCTION(SMPTE2094_10) },
		{ StandardMetadataType::CROP, DECODE_FUNCTION(CROP) },
	};

#ifdef GRALLOC_AML_EXTEND
#define DECODE_FUNCTION_AML(decode)											\
		[](const void *data, size_t data_size, void *out) {					\
			int32_t value;													\
			auto ret = decode(data, data_size, &value);						\
			if (ret != common::mapper_error::NONE)							\
			{																\
				return ret; 												\
			}																\
																			\
			*reinterpret_cast<int32_t *>(out) = value;						\
																			\
			return common::mapper_error::NONE;								\
		}

		static std::unordered_map<AmlMetadataType, common::metadata_decoder> aml_handlers = {
			{ AmlMetadataType::AM_OMX_TUNNEL, DECODE_FUNCTION_AML(decodeAmlInt32Value_stableC) },
			{ AmlMetadataType::AM_OMX_FLAG, DECODE_FUNCTION_AML(decodeAmlInt32Value_stableC) },
			{ AmlMetadataType::AM_OMX_VIDEO_TYPE, DECODE_FUNCTION_AML(decodeAmlInt32Value_stableC) },
			{ AmlMetadataType::AM_OMX_BUFFER_SEQUENCE, DECODE_FUNCTION_AML(decodeAmlInt32Value_stableC) },
		};
#endif

	auto empty_handler = [](const void *, size_t, void *) { return common::mapper_error::BAD_VALUE; };
	if (metadata.is_standard_metadata_type())
	{
		auto type = metadata.get_standard_metadata_type_value();
		auto it = standard_handlers.find(type);
		if (it != standard_handlers.end())
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

AIMapper_Error GrallocMapperV5::setMetadata(buffer_handle_t _Nonnull buffer, AIMapper_MetadataType metadataType,
	const void *_Nonnull metadata, size_t metadataSize)
{
	auto common_metadata = common::metadata_descriptor{ metadataType.name, metadataType.value };
	auto err = common::set(buffer, common_metadata, reinterpret_cast<const uint8_t *>(metadata), metadataSize,
	                       get_decode_function(common_metadata));

	return mapper_error_to_stablec_error(err);
}

AIMapper_Error GrallocMapperV5::setStandardMetadata(buffer_handle_t _Nonnull buffer, int64_t standardMetadataType,
	const void *_Nonnull metadata, size_t metadataSize)
{
	AIMapper_MetadataType standard_metadata{ common::STANDARD_METADATA_NAME, standardMetadataType };
	return setMetadata(buffer, standard_metadata, metadata, metadataSize);
}

AIMapper_Error GrallocMapperV5::getTransportSize(buffer_handle_t _Nonnull buffer, uint32_t *_Nonnull outNumFds,
	uint32_t *_Nonnull outNumInts)
{
	int num_fds = 0;
	int num_ints = 0;
	auto err = common::get_transport_size(buffer, num_fds, num_ints);

	auto error = mapper_error_to_stablec_error(err);
	if (error == AIMAPPER_ERROR_NONE)
	{
		*outNumFds = static_cast<uint32_t>(num_fds);
		*outNumInts = static_cast<uint32_t>(num_ints);
	}
	return error;
}

AIMapper_Error GrallocMapperV5::listSupportedMetadataTypes(
	const AIMapper_MetadataTypeDescription *_Nullable *_Nonnull outDescriptionList,
	size_t *_Nonnull outNumberOfDescriptions)
{
	*outDescriptionList = m_stablec_metadata.data();
	*outNumberOfDescriptions = m_stablec_metadata.size();
	return AIMAPPER_ERROR_NONE;
}

AIMapper_Error GrallocMapperV5::dumpBuffer(buffer_handle_t _Nonnull bufferHandle,
	AIMapper_DumpBufferCallback _Nonnull dumpBufferCallback, void *_Null_unspecified context)
{
	common::buffer_dump buf_dump;
	auto err = common::dump_buffer(bufferHandle, buf_dump, standard_handlers, true);
	if (err != common::mapper_error::NONE)
	{
		return mapper_error_to_stablec_error(err);
	}

	for (auto &it : buf_dump.metadata)
	{
		auto type = AIMapper_MetadataType{ it.type.m_name, it.type.m_value };
		dumpBufferCallback(context, type, it.data.data(), it.data.size());
	}

	return AIMAPPER_ERROR_NONE;
}

AIMapper_Error GrallocMapperV5::dumpAllBuffers(AIMapper_BeginDumpBufferCallback _Nonnull beginDumpBufferCallback,
	AIMapper_DumpBufferCallback _Nonnull dumpBufferCallback, void *_Null_unspecified context)
{
	std::vector<common::buffer_dump> buf_dumps;
	auto err = common::dump_buffers(buf_dumps, standard_handlers, true);
	if (err != common::mapper_error::NONE)
	{
		return mapper_error_to_stablec_error(err);
	}

	beginDumpBufferCallback(context);
	for (auto &it : buf_dumps)
	{
		for (auto &dump : it.metadata)
		{
			auto type = AIMapper_MetadataType{ dump.type.m_name, dump.type.m_value };
			dumpBufferCallback(context, type, dump.data.data(), dump.data.size());
		}
	}

	return AIMAPPER_ERROR_NONE;
}

/* Export necessary symbols for Stable-C implementation */
extern "C" uint32_t ANDROID_HAL_MAPPER_VERSION = AIMAPPER_VERSION_5;

extern "C" AIMapper_Error AIMapper_loadIMapper(AIMapper *_Nullable *_Nonnull outImplementation)
{
	static vendor::mapper::IMapperProvider<GrallocMapperV5> provider;
	return provider.load(outImplementation);
}
