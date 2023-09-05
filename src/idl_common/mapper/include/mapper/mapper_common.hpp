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
#include <android/rect.h>
#include <vector>
#include <unordered_map>

#include "mapper_types.hpp"
#include "mapper_metadata.h"

#include "log.h"
#include "core/buffer_descriptor.h"

using aidl::android::hardware::graphics::common::PlaneLayout;

namespace arm
{
namespace mapper
{
namespace common
{
/**
 * @brief Mapper data that is passed to the encoder for storing
 *        all necessary information for data access and results
 */
struct mapper_data
{
	void *out_data;
	size_t out_data_size;
	int32_t result;
};

/**
 * Imports a raw buffer handle to create an imported buffer handle for use with
 * the rest of the mapper or with other in-process libraries.
 *
 * @param raw_handle   [in] Raw buffer handle to import.
 * @param out_handle   [out] Imported handle.
 * @return mapper_error::BAD_BUFFER for an invalid buffer
 *         mapper_error::NO_RESOURCES when the raw handle cannot be imported
 *         mapper_error::BAD_VALUE when any of the specified attributes are invalid
 *         mapper_error::NONE upon success
 */
mapper_error import_buffer(const native_handle_t *raw_handle, imported_handle **out_handle);

/**
 * Frees a buffer handle and releases all the resources associated with it
 *
 * @param buffer_handle [in] Imported buffer to free
 *
 * @return mapper_error::BAD_BUFFER for an invalid buffer / when failed to free the buffer
 *         mapper_error::NONE on successful free
 */
mapper_error free_buffer(void *buffer_handle);

/*
 * Locks the given buffer for the specified CPU usage.
 *
 * @param buffer        [in]  Buffer to lock.
 * @param cpu_usage     [in]  Specifies one or more CPU usage flags to request
 * @param access_region [in]  Portion of the buffer that the client intends to access.
 * @param acquire_fence [in]  Fence file descriptor
 * @param out_data      [out] CPU accessible buffer address
 *
 * @return mapper_error::BAD_BUFFER for an invalid buffer
 *         mapper_error::NO_RESOURCES when unable to duplicate fence
 *         mapper_error::BAD_VALUE when locking fails
 *         mapper_error::NONE on successful buffer lock
 */
mapper_error lock(const void *buffer, uint64_t cpu_usage, const ARect &access_region, const int acquire_fence,
                  void **out_data);

/*
 * Unlocks a buffer to indicate all CPU accesses to the buffer have completed
 *
 * @param buffer              [in] Buffer to unlock.
 * @param out_release_fence   [out] Fence file descriptor
 *
 * @return mapper_error::BAD_BUFFER for an invalid buffer
 *         mapper_error::BAD_VALUE when unlocking failed
 *         mapper_error::NONE on successful buffer unlock
 */
mapper_error unlock(const void *buffer, int &out_release_fence);

/**
 * Flushes the CPU caches of a mapped buffer.
 *
 * @param buffer   [in] Locked buffer which needs to have CPU caches flushed.
 * @return mapper_error::NONE if success, error value otherwise
 */
mapper_error flush_locked_buffer(const void *buffer);

/**
 * Invalidates the CPU caches of a mapped buffer.
 *
 * @param buffer [in] Locked buffer which needs to have CPU caches invalidated.
 *
 * @return mapper_error::NONE upon success.
 *         mapper_error::BAD_BUFFER for an invalid buffer or a buffer that has not been locked.
 */
mapper_error reread_locked_buffer(const void *buffer);

/**
 * Returns the region of shared memory associated with the buffer that is
 * reserved for client use.
 *
 * The shared memory may be allocated from any shared memory allocator.
 * The shared memory must be CPU-accessible and virtually contiguous. The
 * starting address must be word-aligned.
 *
 * This function may only be called after importBuffer() has been called by the
 * client. The reserved region must remain accessible until freeBuffer() has
 * been called. After freeBuffer() has been called, the client must not access
 * the reserved region.
 *
 * This reserved memory may be used in future versions of Android to
 * help clients implement backwards compatible features without requiring
 * IAllocator/IMapper updates.
 *
 * @param buffer                   [in] Imported buffer handle.
 * @param out_reserved_region      [out] CPU-accessible pointer to the reserved region
 * @param out_reserved_region_size [out] The size of the reservedRegion that was requested
 * @return mapper_error::NONE upon success.
 *         mapper_error::BAD_BUFFER if the buffer is invalid.
 */
mapper_error get_reserved_region(const void *buffer, void **out_reserved_region, uint64_t &out_reserved_region_size);

/**
 * Retrieves a Buffer's metadata value.
 *
 * @param buffer    [in] The buffer to query for metadata.
 * @param metadata  [in] The type of metadata to query.
 * @param output    [out] The output buffer that will be populated with the metadata
 * @param encoder   [in] The encoder for the metadata.
 * @return mapper_error::NONE on success.
 *         mapper_error::BAD_BUFFER on invalid buffer argument.
 *         mapper_error::UNSUPPORTED on error when reading or unsupported metadata type.
 */
mapper_error get(const void *buffer, const metadata_descriptor &metadata, std::vector<uint8_t> &output,
                 metadata_encoder encoder);

/**
 * Sets a Buffer's metadata value.
 *
 * @param buffer    [in] The buffer to query for metadata.
 * @param metadata  [in] The type of metadata to set.
 * @param data      [out] The data that will be used to populate metadata
 * @param data_size [in] The data size
 * @param decoder   [in] The decoder for the metadata.
 * @return mapper_error::NONE on success.
 *         mapper_error::BAD_BUFFER on invalid buffer argument.
 *         mapper_error::UNSUPPORTED on error when reading or unsupported metadata type.
 */
mapper_error set(const void *buffer, const metadata_descriptor &metadata, const uint8_t *data, size_t data_size,
                 metadata_decoder decoder);

/**
 * Get the transport size of a buffer
 *
 * @param buffer       [in] Buffer for computing transport size
 * @param out_num_fds  [out] Number of file descriptors needed for transport
 * @param out_num_ints [out] Number of integers needed for transport
 * @return mapper_error::NONE on success.
 *         mapper_error::BAD_BUFFER for an invalid buffer.
 *         mapper_error::UNSUPPORTED on error when reading or unsupported metadata type.
 */
mapper_error get_transport_size(const void *buffer, int &out_num_fds, int &out_num_ints);

/**
 * Lists all the MetadataTypes supported by IMapper as well as a description
 * of each supported MetadataType. For StandardMetadataTypes, the description
 * string can be left empty.
 *
 * @return vector of MetadataTypeDescriptions that represent the
 *         MetadataTypes supported by the device
 */
const std::vector<metadata_type> &list_supported_metadata_types();

/**
 * Dumps a buffer's metadata.
 *
 * @param buffer           [in] Buffer that is being dumped
 * @param out_buffer_dump  [out] Struct representing the metadata being dumped
 * @param encoders         [in] Encoders for standard AIDL metadata types
 * @return mapper_error::NONE on success.
 *         mapper_error::BAD_BUFFER if the raw handle is invalid.
 *         mapper_error::NO_RESOURCES if the get cannot be fullfilled due to unavailability of
 *                                    resources.
 */
mapper_error dump_buffer(const void *buffer, buffer_dump &out_buffer_dump,
                         const std::unordered_map<StandardMetadataType, metadata_encoder> &encoders,
                         bool isMapperV5);

/**
 * Dumps the metadata for all the buffers in the current process.
 *
 * @param out_buffer_dumps  [out] Vector of structs representing the buffers being dumped
 * @param encoders          [in] Encoders for standard AIDL metadata types
 * @return mapper_error::NONE on success.
 *         mapper_error::BAD_BUFFER if the raw handle is invalid.
 *         mapper_error::NO_RESOURCES if the get cannot be fullfilled due to unavailability of
 *                                    resources.
 */
mapper_error dump_buffers(std::vector<buffer_dump> &out_buffer_dumps,
                          const std::unordered_map<StandardMetadataType, metadata_encoder> &encoders,
                          bool isMapperV5);

/**
 * @brief Get the plane layouts from buffer handle
 *
 * @param handle  [in] Buffer handle
 * @param layouts [out] Plane layouts
 * @return mapper_error::NONE on success.
 *         mapper_error::BAD_VALUE if buffer has invalid format.
 */
mapper_error get_plane_layouts(const private_handle_t *handle, std::vector<PlaneLayout> *layouts);

} // namespace common
} // namespace mapper
} // namespace arm
