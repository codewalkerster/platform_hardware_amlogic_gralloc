/*
 * Copyright (C) 2016-2023 Arm Limited. All rights reserved.
 *
 * Copyright (C) 2008 The Android Open Source Project
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

#pragma once

#include <string>

#include "buffer.h"
#include "internal_format.h"

#define MAX_NAME_LENGTH 127
#define NAME_BUFFER_SIZE 128

/* Flags to describe additional buffer descriptor information */
enum buffer_descriptor_flags : uint32_t
{
	GPU_DATA_BUFFER_WITH_ANY_FORMAT = 1,
	USE_AIDL_FRONTBUFFER_USAGE = 1 << 1,
	SUPPORTS_R8 = 1 << 2,
	HWC_FB_DISABLE_AFBC = 1 << 3,
	HWC_FORCE_BGRA_8888 = 1 << 4,
	/* This flag is used to mark support in R16 and RG16 as part
	of A14 formats*/
	SUPPORTS_R16_RG16 = 1 << 5,
	/* This flag is used to mark support in A13 special
	use case of IMPLEMENTATION_DEFINED format */
	HW_IMP_CAM_USAGE = 1 << 6,
};

/* A buffer_descriptor contains the requested parameters for the buffer
 * as well as the calculated parameters that are passed to the allocator.
 */
struct buffer_descriptor_t
{
	/* For validation. */
	uint32_t signature{};

	/* Requested parameters from IAllocator. */
	uint32_t width{};
	uint32_t height{};
	uint64_t producer_usage{};
	uint64_t consumer_usage{};
	uint64_t hal_format{};
	uint32_t layer_count{};
	std::array<unsigned char, NAME_BUFFER_SIZE> name{};
	uint64_t reserved_size{};

	/*
	 * Calculated values that will be passed to the allocator in order to
	 * allocate the buffer.
	 */
	size_t size{};
	int pixel_stride{};
	internal_format_t alloc_format{};
	plane_layout plane_info{};

	std::underlying_type_t<buffer_descriptor_flags> flags{};
};
