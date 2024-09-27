/*
 * Copyright (C) 2022-2023 Arm Limited.
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

#include "allocator.h"
#include "core/buffer_allocation.h"
#include "idl_common/descriptor.h"
#include "idl_common/allocator.h"
#include "allocator/allocator.h"
#include "usages.h"

#include <aidlcommonsupport/NativeHandle.h>
#include <aidl/android/hardware/graphics/allocator/AllocationError.h>
#include <android/binder_status.h>
#include <algorithm>

namespace aidl::android::hardware::graphics::allocator::impl::arm
{

using ::android::hardware::hidl_vec;

static ndk::ScopedAStatus allocate_common(buffer_descriptor_t *buffer_descriptor, int32_t in_count,
                                          AllocationResult *out_result)
{
	buffer_descriptor->flags |= GPU_DATA_BUFFER_WITH_ANY_FORMAT | SUPPORTS_R8 | USE_AIDL_FRONTBUFFER_USAGE;
#ifdef GRALLOC_HWC_FB_DISABLE_AFBC
	buffer_descriptor->flags |= (GRALLOC_HWC_FB_DISABLE_AFBC) ? HWC_FB_DISABLE_AFBC : 0;
#endif
#ifdef GRALLOC_HWC_FORCE_BGRA_8888
	buffer_descriptor->flags |= (GRALLOC_HWC_FORCE_BGRA_8888) ? HWC_FORCE_BGRA_8888 : 0;
#endif
#if PLATFORM_SDK_VERSION > 33
	buffer_descriptor->flags |= SUPPORTS_R16_RG16;
#else
	buffer_descriptor->flags |= HW_IMP_CAM_USAGE;
#endif

	CHECK_EQ(buffer_descriptor->flags, ::arm::mapper::common::DESCRIPTOR_ALLOCATOR_FLAGS);

	auto result = ::arm::allocator::common::allocate(buffer_descriptor, in_count);
	if (!result.has_value())
	{
		switch (result.error())
		{
		case ::android::NO_ERROR:
			break;
		case ::android::NO_MEMORY:
			return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(AllocationError::NO_RESOURCES));
		case ::android::BAD_VALUE:
			return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(AllocationError::UNSUPPORTED));
		default:
			MALI_GRALLOC_LOGE("Unknown allocation error %d\n", result.error());
			return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(AllocationError::UNSUPPORTED));
		}
	}

	CHECK_EQ(in_count, result->size());

	out_result->stride = buffer_descriptor->pixel_stride;
	out_result->buffers.reserve(in_count);
	/* Pass ownership when returning the created handles. */
	for (auto &handle : *result)
	{
		auto handle_to_move = handle.release();
		out_result->buffers.emplace_back(::android::makeToAidl(handle_to_move));
		native_handle_delete(handle_to_move);
	}

	return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus allocator::allocate(const std::vector<uint8_t> &in_descriptor, int32_t in_count,
                                       AllocationResult *out_result)
{
	buffer_descriptor_t buffer_descriptor;
	hidl_vec<uint8_t> hidl_descriptor(in_descriptor);
	if (!::arm::mapper::common::grallocDecodeBufferDescriptor(hidl_descriptor, buffer_descriptor))
	{
		return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(AllocationError::BAD_DESCRIPTOR));
	}

	return allocate_common(&buffer_descriptor, in_count, out_result);
}
allocator::allocator()
{
	get_debug_log_level();
}
#if GRALLOC_ALLOCATOR_AIDL_VERSION >= 2

ndk::ScopedAStatus allocator::allocate2(const BufferDescriptorInfo &in_descriptor, int32_t in_count,
	AllocationResult *out_result)
{
	std::string name{reinterpret_cast<const char*>(in_descriptor.name.data())};
	if (name == "gralloc_debug_log")
	{
		get_debug_log_level();
		return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(AllocationError::UNSUPPORTED));
	}
	auto supported = false;
	auto status = allocator::isSupported(in_descriptor, &supported);
	if (!status.isOk())
	{
		MALI_GRALLOC_LOGE("allocator::isSupported failed.");
		return status;
	}

	if (!supported)
	{
		MALI_GRALLOC_LOGE("Unsupported BufferDescriptorInfo.");
		return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(AllocationError::UNSUPPORTED));
	}

	buffer_descriptor_t buffer_descriptor;
	buffer_descriptor.width = in_descriptor.width;
	buffer_descriptor.height = in_descriptor.height;
	buffer_descriptor.hal_format = static_cast<uint64_t>(in_descriptor.format);
	buffer_descriptor.producer_usage = static_cast<uint64_t>(in_descriptor.usage);
	buffer_descriptor.consumer_usage = buffer_descriptor.producer_usage;
	buffer_descriptor.layer_count = in_descriptor.layerCount;
	buffer_descriptor.name = in_descriptor.name;
	buffer_descriptor.reserved_size = in_descriptor.reservedSize;
	/* BufferDecsriptorInfo.additionalOptions can be used as another way to allocate AFRC formats in the future */
	return allocate_common(&buffer_descriptor, in_count, out_result);
}

ndk::ScopedAStatus allocator::isSupported(const BufferDescriptorInfo &in_descriptor, bool *out_result)
{
	buffer_descriptor_t grallocDescriptor;
	grallocDescriptor.width = in_descriptor.width;
	grallocDescriptor.height = in_descriptor.height;
	grallocDescriptor.layer_count = in_descriptor.layerCount;
	grallocDescriptor.hal_format = static_cast<uint64_t>(in_descriptor.format);
	grallocDescriptor.producer_usage = static_cast<uint64_t>(in_descriptor.usage);
	grallocDescriptor.consumer_usage = grallocDescriptor.producer_usage;

	grallocDescriptor.flags |= GPU_DATA_BUFFER_WITH_ANY_FORMAT | SUPPORTS_R8 | USE_AIDL_FRONTBUFFER_USAGE;
#ifdef GRALLOC_HWC_FB_DISABLE_AFBC
	grallocDescriptor.flags |= (GRALLOC_HWC_FB_DISABLE_AFBC) ? HWC_FB_DISABLE_AFBC : 0;
#endif
#ifdef GRALLOC_HWC_FORCE_BGRA_8888
	grallocDescriptor.flags |= (GRALLOC_HWC_FORCE_BGRA_8888) ? HWC_FORCE_BGRA_8888 : 0;
#endif
#if PLATFORM_SDK_VERSION > 33
	grallocDescriptor.flags |= SUPPORTS_R16_RG16;
#else
	grallocDescriptor.flags |= HW_IMP_CAM_USAGE;
#endif

	*out_result = true;

	/* Check if it is possible to allocate a buffer for the given description */
	const int result = mali_gralloc_derive_format_and_size(&grallocDescriptor);
	if (result != 0)
	{
		MALI_GRALLOC_LOGV("Allocation for the given description will not succeed. error: %d", result);
		*out_result = false;
		return ndk::ScopedAStatus::ok();
	}

	if (grallocDescriptor.producer_usage & GRALLOC_USAGE_PROTECTED)
	{
		if (!allocator_supports_protected_memory(&grallocDescriptor))
		{
			MALI_GRALLOC_LOGV("Protected memory allocation for the given description will not succeed for format(%" PRIu64 ").", grallocDescriptor.hal_format);
			*out_result = false;
			return ndk::ScopedAStatus::ok();
		}
	}

	const std::array<std::string, 0> supported_options{};
	for (const auto &option : in_descriptor.additionalOptions)
	{
		auto option_found =
		    std::find_if(std::begin(supported_options), std::end(supported_options),
		                 [&option](const std::string &supported_option) { return option.name == supported_option; });
		if (option_found == std::end(supported_options))
		{
			MALI_GRALLOC_LOGV("additional Options has unsupported option allocation will fail.");
			*out_result = false;
			return ndk::ScopedAStatus::ok();
		}
	}

	return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus allocator::getIMapperLibrarySuffix(std::string *out_result)
{
#if defined(GRALLOC_STABLEC_MAPPER_ENABLED) && GRALLOC_STABLEC_MAPPER_ENABLED == 1
	*out_result = "arm";
	return ndk::ScopedAStatus::ok();
#else
	(void)(out_result);
	return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
#endif
}

#endif // GRALLOC_ALLOCATOR_AIDL_VERSION >= 2

} // namespace aidl::android::hardware::graphics::allocator::impl::arm
