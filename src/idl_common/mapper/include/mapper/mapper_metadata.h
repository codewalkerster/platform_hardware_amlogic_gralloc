/*
 * Copyright (C) 2020-2023 Arm Limited. All rights reserved.
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
#pragma once

#include <inttypes.h>

#include "log.h"
#include "core/buffer_descriptor.h"
#include "core/buffer.h"
#include "idl_common/shared_metadata.h"
#include "mapper/mapper_common.hpp"
#include "mapper/mapper_types.hpp"

#include <aidl/arm/graphics/Compression.h>
#include <aidl/arm/graphics/ArmMetadataType.h>
#include <aidl/arm/graphics/AmlMetadataType.h>

using aidl::android::hardware::graphics::common::StandardMetadataType;
using aidl::arm::graphics::ArmMetadataType;
using aidl::arm::graphics::AmlMetadataType;

namespace arm
{
namespace mapper
{
namespace common
{
using aidl::android::hardware::graphics::common::ExtendableType;

#define GRALLOC_ARM_COMPRESSION_TYPE_NAME "arm.graphics.Compression"
const static ExtendableType Compression_AFBC{ GRALLOC_ARM_COMPRESSION_TYPE_NAME,
	                                          static_cast<int64_t>(aidl::arm::graphics::Compression::AFBC) };

const static ExtendableType Compression_AFRC{ GRALLOC_ARM_COMPRESSION_TYPE_NAME,
	                                          static_cast<int64_t>(aidl::arm::graphics::Compression::AFRC) };

#define GRALLOC_ARM_METADATA_TYPE_NAME "arm.graphics.ArmMetadataType"

const static metadata_descriptor ArmMetadataType_PLANE_FDS{
	GRALLOC_ARM_METADATA_TYPE_NAME, static_cast<int64_t>(aidl::arm::graphics::ArmMetadataType::PLANE_FDS)
};

const static metadata_descriptor ArmMetadataType_FORMAT_DATA_TYPE{
	GRALLOC_ARM_METADATA_TYPE_NAME, static_cast<int64_t>(aidl::arm::graphics::ArmMetadataType::FORMAT_DATA_TYPE)
};

#ifdef GRALLOC_AML_EXTEND
#define GRALLOC_AML_METADATA_TYPE_NAME "arm.graphics.AmlMetadataType"
const static metadata_descriptor AmlMetadataType_AM_OMX_TUNNEL{
    GRALLOC_AML_METADATA_TYPE_NAME, static_cast<int64_t>(aidl::arm::graphics::AmlMetadataType::AM_OMX_TUNNEL)
};

const static metadata_descriptor AmlMetadataType_AM_OMX_FLAG{
    GRALLOC_AML_METADATA_TYPE_NAME, static_cast<int64_t>(aidl::arm::graphics::AmlMetadataType::AM_OMX_FLAG)
};

const static metadata_descriptor AmlMetadataType_AM_OMX_VIDEO_TYPE{
    GRALLOC_AML_METADATA_TYPE_NAME, static_cast<int64_t>(aidl::arm::graphics::AmlMetadataType::AM_OMX_VIDEO_TYPE)
};

const static metadata_descriptor AmlMetadataType_AM_OMX_BUFFER_SEQUENCE{
    GRALLOC_AML_METADATA_TYPE_NAME, static_cast<int64_t>(aidl::arm::graphics::AmlMetadataType::AM_OMX_BUFFER_SEQUENCE)
};
#endif

/**
 * Retrieves a Buffer's metadata value.
 *
 * @param handle       [in] The private handle of the buffer to query for metadata.
 * @param metadata     [in] The type of metadata queried.
 * @param output       [out] Output buffer with the encoded metadata.
 * @param encode_fn    [in] Encoder for the metadata.
 *
 * @return mapper_error::NONE on success.
 *         mapper_error::BAD_BUFFER on invalid buffer argument.
 *         mapper_error::UNSUPPORTED on error when reading or unsupported metadata type.
 */
mapper_error get_metadata(const private_handle_t *handle, const metadata_descriptor &metadata,
                          std::vector<uint8_t> &output, metadata_encoder encode_fn);

/**
 * Sets a Buffer's metadata value.
 *
 * @param handle       [in] The private handle of the buffer for which to modify metadata.
 * @param metadata     [in] The type of metadata to modify.
 * @param data         [in] Input buffer with the metadata.
 * @param data_size    [in] Size of the input buffer in bytes.
 * @param decode_fn    [in] Decoder for the metadata buffer.
 *
 * @return mapper_error::NONE on success.
 *         mapper_error::BAD_BUFFER on invalid buffer argument.
 *         mapper_error::UNSUPPORTED on error when writing or unsupported metadata type.
 */
mapper_error set_metadata(const imported_handle *handle, const metadata_descriptor &metadata, const uint8_t *data,
                          size_t data_size, metadata_decoder decode_fn);

} // namespace common
} // namespace mapper
} // namespace arm
