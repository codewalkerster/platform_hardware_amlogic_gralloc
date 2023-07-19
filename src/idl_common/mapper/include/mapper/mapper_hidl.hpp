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

#include "mapper/mapper_metadata.h"
#include "4.x/mapper/mapper_hidl_header.h"

namespace arm
{
namespace mapper
{
namespace hidl
{

void is_supported(const IMapper::BufferDescriptorInfo &description, IMapper::isSupported_cb hidl_cb);

void get_from_buffer_descriptor_info(IMapper::BufferDescriptorInfo const &description,
                                     IMapper::MetadataType const &metadataType,
                                     IMapper::getFromBufferDescriptorInfo_cb hidl_cb);
/**
 * Validates the buffer against specified descriptor attributes
 *
 * @param buffer          [in] Buffer which needs to be validated.
 * @param descriptorInfo  [in] Required attributes of the buffer
 * @param in_stride       [in] Buffer stride returned by IAllocator::allocate,
 *                             or zero if unknown.
 *
 * @return Error::NONE upon success. Otherwise,
 *         Error::BAD_BUFFER upon bad buffer input
 *         Error::BAD_VALUE when any of the specified attributes are invalid
 */
Error validate_buffer_size(void *buffer, const IMapper::BufferDescriptorInfo &descriptorInfo, uint32_t in_stride);

} // namespace hidl
} // namespace mapper
} // namespace arm
