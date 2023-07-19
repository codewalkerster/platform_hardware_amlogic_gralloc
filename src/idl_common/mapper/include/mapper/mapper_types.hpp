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

#pragma once

#include <inttypes.h>
#include <functional>
#include <cstring>
#include <vector>

#include <aidl/arm/graphics/ArmMetadataType.h>
#include <aidl/arm/graphics/AmlMetadataType.h>
#include <aidl/android/hardware/graphics/common/StandardMetadataType.h>
#include <gralloctypes/Gralloc4.h>
#include <utils/Errors.h>

using aidl::android::hardware::graphics::common::StandardMetadataType;
using aidl::arm::graphics::ArmMetadataType;
using aidl::arm::graphics::AmlMetadataType;

namespace arm
{
namespace mapper
{
namespace common
{

constexpr const char *STANDARD_METADATA_NAME = "android.hardware.graphics.common.StandardMetadataType";
constexpr const char *ARM_METADATA_NAME = "arm.graphics.ArmMetadataType";
constexpr const char *AML_METADATA_NAME = "arm.graphics.AmlMetadataType";

enum class mapper_error : int32_t
{
	/**
	 * No error.
	 */
	NONE = 0,
	/**
	 * Invalid BufferDescriptor.
	 */
	BAD_DESCRIPTOR = 1,
	/**
	 * Invalid buffer handle.
	 */
	BAD_BUFFER = 2,
	/**
	 * Invalid HardwareBufferDescription.
	 */
	BAD_VALUE = 3,
	/**
	 * Resource unavailable.
	 */
	NO_RESOURCES = 5,
	/**
	 * Permanent failure.
	 */
	UNSUPPORTED = 7,
};

static mapper_error android_err_to_mapper_err(android::status_t error)
{
	switch (error)
	{
	case android::OK:
		return mapper_error::NONE;
	case android::BAD_VALUE:
		return mapper_error::BAD_VALUE;
	default:
		return mapper_error::UNSUPPORTED;
	}
}

struct metadata_descriptor
{
	const char *m_name;
	int64_t m_value;

	metadata_descriptor(const char *name, int64_t value)
	    : m_name(name)
	    , m_value(value)
	{
	}

	bool is_standard_metadata_type() const
	{
		return std::strcmp(STANDARD_METADATA_NAME, m_name) == 0;
	}

	StandardMetadataType get_standard_metadata_type_value() const
	{
		return static_cast<StandardMetadataType>(m_value);
	}

	bool is_arm_metadata_type() const
	{
		return std::strcmp(ARM_METADATA_NAME, m_name) == 0;
	}

	ArmMetadataType get_arm_metadata_type_value() const
	{
		return static_cast<ArmMetadataType>(m_value);
	}

	bool is_aml_metadata_type() const
	{
		return std::strcmp(AML_METADATA_NAME, m_name) == 0;
	}

	AmlMetadataType get_aml_metadata_type_value() const
	{
		return static_cast<AmlMetadataType>(m_value);
	}
};

inline bool operator==(const metadata_descriptor &left, const metadata_descriptor &right)
{
	return !std::strcmp(left.m_name, right.m_name) && (left.m_value == right.m_value);
}

struct metadata_type
{
	metadata_type(StandardMetadataType type, bool is_gettable, bool is_settable)
	    : m_descriptor(GRALLOC4_STANDARD_METADATA_TYPE, static_cast<int64_t>(type))
	    , m_description(nullptr)
	    , m_is_gettable(is_gettable)
	    , m_is_settable(is_settable)
	{
	}

	metadata_type(metadata_descriptor custom_type, const char *description, bool is_gettable, bool is_settable)
	    : m_descriptor(custom_type)
	    , m_description(description)
	    , m_is_gettable(is_gettable)
	    , m_is_settable(is_settable)
	{
	}

	metadata_descriptor m_descriptor;
	const char *m_description;
	bool m_is_gettable;
	bool m_is_settable;
};

struct metadata_dump
{
	metadata_descriptor type;
	std::vector<uint8_t> data;
};

struct buffer_dump
{
	std::vector<metadata_dump> metadata;
};

using metadata_decoder = std::function<mapper_error(const uint8_t *, size_t, void *)>;
using metadata_encoder = std::function<mapper_error(const void *, std::vector<uint8_t> *)>;

} // namespace common
} // namespace mapper
} // namespace arm
