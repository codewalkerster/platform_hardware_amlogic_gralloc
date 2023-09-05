/*
 * Copyright (C) 2020-2023 Arm Limited.
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

#include "mapper/mapper_common.hpp"
#include "aidl/android/hardware/graphics/common/StandardMetadataType.h"
#include "mapper/mapper_types.hpp"
#include "registered_handle_pool.h"

#include "idl_common/descriptor.h"
#include "idl_common/shared_metadata.h"

#include "core/buffer_allocation.h"
#include "core/buffer_descriptor.h"
#include "core/buffer_access.h"
#include "core/format_info.h"
#include "core/drm_utils.h"
#include "core/buffer.h"

#include "allocator/allocator.h"
#include "log.h"
#include "gralloc/formats.h"
#include "usages.h"

#include <sync/sync.h>
/* For error codes. */
#include <hardware/gralloc1.h>
#include <unordered_map>

#include "mapper/mapper_metadata.h"
#include "utils/Errors.h"

namespace arm
{
namespace mapper
{
namespace common
{

using aidl::android::hardware::graphics::common::PlaneLayoutComponent;

/*
 * {MetadataType metadataType, string description, bool isGettable, bool isSettable}
 * Only non-standard types require a description.
 */
static const std::vector<metadata_type> metadata_descriptions = {
	{ StandardMetadataType::BUFFER_ID, true, false },
	{ StandardMetadataType::NAME, true, false },
	{ StandardMetadataType::WIDTH, true, false },
	{ StandardMetadataType::HEIGHT, true, false },
	{ StandardMetadataType::LAYER_COUNT, true, false },
	{ StandardMetadataType::PIXEL_FORMAT_REQUESTED, true, false },
	{ StandardMetadataType::PIXEL_FORMAT_FOURCC, true, false },
	{ StandardMetadataType::PIXEL_FORMAT_MODIFIER, true, false },
	{ StandardMetadataType::USAGE, true, false },
	{ StandardMetadataType::ALLOCATION_SIZE, true, false },
	{ StandardMetadataType::PROTECTED_CONTENT, true, false },
	{ StandardMetadataType::COMPRESSION, true, false },
	{ StandardMetadataType::INTERLACED, true, false },
	{ StandardMetadataType::CHROMA_SITING, true, true },
	{ StandardMetadataType::PLANE_LAYOUTS, true, false },
	{ StandardMetadataType::DATASPACE, true, true },
	{ StandardMetadataType::BLEND_MODE, true, true },
	{ StandardMetadataType::SMPTE2086, true, true },
	{ StandardMetadataType::CTA861_3, true, true },
	{ StandardMetadataType::SMPTE2094_40, true, true },
	{ StandardMetadataType::CROP, true, true },
#if PLATFORM_SDK_VERSION >= 33
	{ StandardMetadataType::SMPTE2094_10, true, true },
#endif
#if defined(GRALLOC_STABLEC_MAPPER_ENABLED) && GRALLOC_STABLEC_MAPPER_ENABLED == 1
	{ StandardMetadataType::STRIDE, true, false },
#endif
	/* Arm vendor metadata */
	{ ArmMetadataType_PLANE_FDS, "Vector of file descriptors of each plane", true, false },
	{ ArmMetadataType_FORMAT_DATA_TYPE, "Format data type", true, false },
#ifdef GRALLOC_AML_EXTEND
	{ AmlMetadataType_AM_OMX_TUNNEL,
		"set tunnel index for omx video for pip", true, true },
	{ AmlMetadataType_AM_OMX_FLAG,
		"Extend by aml for update the omx flag pts/v4l", true, true },
	{ AmlMetadataType_AM_OMX_VIDEO_TYPE,
		"Extend by aml for update the omx video_type", true, true },
	{ AmlMetadataType_AM_OMX_BUFFER_SEQUENCE,
		"Extend by aml for update the omx buffer sequence", true, true },
#endif
};

mapper_error import_buffer(const native_handle_t *raw_handle, imported_handle **out_handle)
{
	auto private_handle = handle_cast<private_handle_t>(raw_handle);
	if (private_handle == nullptr)
	{
		MALI_GRALLOC_LOGE("%s: Invalid buffer handle to import", __FUNCTION__);
		return mapper_error::BAD_BUFFER;
	}
	auto import_handle = make_imported_handle(private_handle);
	if (import_handle == nullptr)
	{
		MALI_GRALLOC_LOGE("%s: Failed to clone buffer handle", __FUNCTION__);
		return mapper_error::NO_RESOURCES;
	}

	int protection = PROT_READ | PROT_WRITE;
	int flags = MAP_SHARED;
	off_t page_offset = 0;
	import_handle->attr_base =
	    mmap(nullptr, import_handle->attr_size, protection, flags, import_handle->share_attr_fd, page_offset);
	if (import_handle->attr_base == MAP_FAILED)
	{
		MALI_GRALLOC_LOGE("%s: Failed to call mmap for attr_base. size = %" PRIu64,
			__FUNCTION__, import_handle->attr_size);
		return mapper_error::NO_RESOURCES;
	}

	auto unmap = android::base::make_scope_guard(
	    [&import_handle]() { munmap(import_handle->attr_base, import_handle->attr_size); });

	import_handle->import_pid = getpid();

	/* Cloned buffers don't share the same buffer mapping */
	import_handle->base = nullptr;
	import_handle->cpu_write = 0;

	if (!RegisteredHandlePool::get_instance().add(import_handle.get()))
	{
		MALI_GRALLOC_LOGW("Handle %p already present in pool of registered handles. This can happen if the buffer was "
		                  "freed by a different mapper instance and then re-allocated",
		                  import_handle.get());
	}

	assert(import_handle->numFds == PRIVATE_HANDLE_NUM_FDS);
	assert(import_handle->numInts == PRIVATE_HANDLE_NUM_INTS);

	unmap.Disable();
	*out_handle = import_handle.release();

	AML_GRALLOC_LOGI("%s: raw_handle(%p) out_handle(%p)", __FUNCTION__, raw_handle, *out_handle);
	return mapper_error::NONE;
}

mapper_error free_buffer(void *buffer_handle)
{
	auto handle = unique_imported_handle{ handle_cast<imported_handle>(static_cast<native_handle *>(buffer_handle)) };
	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("Buffer to free: %p is invalid", buffer_handle);
		return mapper_error::BAD_BUFFER;
	}

	if (!RegisteredHandlePool::get_instance().remove(handle.get()))
	{
		MALI_GRALLOC_LOGW("Handle %p not found in pool of registered handles. This can happen if the buffer was "
		                  "imported by a different mapper instance",
		                  buffer_handle);
	}

	if (handle->import_pid == getpid())
	{
		mali_unmap_buffer(handle.get());
		munmap(handle->attr_base, handle->attr_size);
	}

	handle->attr_base = MAP_FAILED;
	handle->import_pid = -1;

	return mapper_error::NONE;
}

/*
 * Locks the given buffer for the specified CPU usage.
 *
 * @param bufferHandle [in]  Buffer to lock.
 * @param cpuUsage     [in]  Specifies one or more CPU usage flags to request
 * @param accessRegion [in]  Portion of the buffer that the client intends to access.
 * @param fenceFd      [in]  Fence file descriptor
 * @param outData      [out] CPU accessible buffer address
 *
 * @return Error::BAD_BUFFER for an invalid buffer
 *         Error::NO_RESOURCES when unable to duplicate fence
 *         Error::BAD_VALUE when locking fails
 *         Error::NONE on successful buffer lock
 */
static mapper_error lock_buffer(buffer_handle_t buffer_handle, uint64_t cpu_usage, const ARect &access_region,
                                int fence_fd, void **out_data)
{
	auto handle = handle_cast<imported_handle>(buffer_handle);
	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("Buffer: %p is corrupted", buffer_handle);
		return mapper_error::BAD_BUFFER;
	}

	if (handle->cpu_write != 0 && (cpu_usage & BufferUsage::CPU_WRITE_MASK) &&
	    handle->alloc_format.get_base() != MALI_GRALLOC_FORMAT_INTERNAL_BLOB)
	{
		MALI_GRALLOC_LOGE("Attempt to call lock*() for writing on an already locked buffer (%p)", buffer_handle);
		/*
		 * Locking a buffer which is not one dimensional multiple times for writing, leads
		 * to undefined behavior. In order to pass the mapper VTS test, we
		 * don't fail here and increase the lock count instead as if the buffer had been locked
		 * successfully.
		 */
		handle->invalid_write_locks = true;
		handle->lock_count++;
		return mapper_error::NONE;
	}

	void *data = nullptr;
	if (fence_fd >= 0)
	{
		sync_wait(fence_fd, -1);
	}

	auto result =
	    mali_gralloc_lock(handle, cpu_usage, access_region.left, access_region.top,
	                      access_region.right - access_region.left, access_region.bottom - access_region.top, &data);
	if (result != 0)
	{
		MALI_GRALLOC_LOGE("Locking buffer failed with error: %d", result);
		if (result == GRALLOC1_ERROR_UNSUPPORTED)
		{
			return mapper_error::BAD_BUFFER;
		}

		return result == -EINVAL ? mapper_error::BAD_VALUE : mapper_error::NO_RESOURCES;
	}

	*out_data = data;
	return mapper_error::NONE;
}

mapper_error lock(const void *buffer, uint64_t cpu_usage, const ARect &access_region, const int acquire_fence,
                  void **out_data)
{
	auto handle = handle_cast<imported_handle>(static_cast<buffer_handle_t>(buffer));
	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("Buffer to lock: %p is invalid", buffer);
		return mapper_error::BAD_BUFFER;
	}

	void *data = nullptr;
	auto err = lock_buffer(handle, cpu_usage, access_region, acquire_fence, &data);
	if (err == mapper_error::NONE)
	{
		*out_data = data;
	}

	return err;
}

/*
 * Unlocks a buffer to indicate all CPU accesses to the buffer have completed
 *
 * @param buffer_handle [in]  Buffer to lock.
 * @param out_fence_fd  [out] Fence file descriptor
 *
 * @return Error::BAD_BUFFER for an invalid buffer
 *         Error::BAD_VALUE when unlocking failed
 *         Error::NONE on successful buffer unlock
 */
static mapper_error unlock_buffer(buffer_handle_t buffer_handle, int *out_fence_fd)
{
	auto handle = handle_cast<imported_handle>(buffer_handle);
	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("Buffer: %p is corrupted", buffer_handle);
		return mapper_error::BAD_BUFFER;
	}

	if (handle->lock_count == 0)
	{
		MALI_GRALLOC_LOGE("Attempt to call unlock*() on an unlocked buffer (%p)", buffer_handle);
		return mapper_error::BAD_BUFFER;
	}

	if (handle->invalid_write_locks)
	{
		MALI_GRALLOC_LOGE("Attempt to call unlock*() on an buffer locked with invalid write locks buffer (%p)",
		                  buffer_handle);
	}

	auto result = mali_gralloc_unlock(handle);
	if (result != 0)
	{
		MALI_GRALLOC_LOGE("Unlocking failed with error: %d", result);
		return mapper_error::BAD_VALUE;
	}

	*out_fence_fd = -1;
	return mapper_error::NONE;
}

mapper_error unlock(const void *buffer, int &out_release_fence)
{
	auto buffer_handle = handle_cast<imported_handle>(static_cast<buffer_handle_t>(buffer));
	if (!buffer_handle)
	{
		MALI_GRALLOC_LOGE("unlock: %p has not been imported", buffer);
		return mapper_error::BAD_BUFFER;
	}

	int release_fence;
	auto err = unlock_buffer(buffer_handle, &release_fence);
	if (err != mapper_error::NONE)
	{
		return err;
	}

	out_release_fence = release_fence;
	return err;
}

mapper_error flush_locked_buffer(const void *buffer)
{
	auto handle = handle_cast<imported_handle>(static_cast<buffer_handle_t>(buffer));
	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("Bandle: %p is corrupted", buffer);
		return mapper_error::BAD_BUFFER;
	}

	if (handle->lock_count == 0)
	{
		MALI_GRALLOC_LOGE("Attempt to call flushLockedBuffer() on an unlocked buffer (%p)", buffer);
		return mapper_error::BAD_BUFFER;
	}

	allocator_sync_end(handle, false, true);
	return mapper_error::NONE;
}

mapper_error reread_locked_buffer(const void *buffer)
{
	auto handle = handle_cast<imported_handle>(static_cast<buffer_handle_t>(buffer));
	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("Buffer: %p is corrupted", buffer);
		return mapper_error::BAD_BUFFER;
	}

	if (handle->lock_count == 0)
	{
		MALI_GRALLOC_LOGE("Attempt to call rereadLockedBuffer() on an unlocked buffer (%p)", buffer);
		return mapper_error::BAD_BUFFER;
	}

	allocator_sync_start(handle, true, false);
	return mapper_error::NONE;
}

static bool is_mutable(const metadata_descriptor &metadata)
{
	const auto it = std::find_if(std::begin(metadata_descriptions), std::end(metadata_descriptions),
	                             [&metadata](const auto &it) { return it.m_descriptor == metadata; });
	return it != std::end(metadata_descriptions) && it->m_is_settable;
}

mapper_error get(const void *buffer, const metadata_descriptor &metadata, std::vector<uint8_t> &output,
                 metadata_encoder encoder)
{
	auto handle = handle_cast<imported_handle>(static_cast<buffer_handle_t>(buffer));
	if (handle == nullptr)
	{
		if (is_mutable(metadata))
		{
			MALI_GRALLOC_LOGE("get: %p has not been imported", buffer);
			return mapper_error::BAD_BUFFER;
		}
		else
		{
			/*
			 * Some clients erroneously pass raw handles. To avoid critical clients
			 * from crashing, we permit metadata to be retrieved from raw handles as
			 * long as the data is immutable.
			 */
			MALI_GRALLOC_LOGV("get: %p has not been imported", buffer);
		}
	}

	auto private_handle = handle_cast<private_handle_t>(static_cast<buffer_handle_t>(buffer));
	if (private_handle == nullptr)
	{
		MALI_GRALLOC_LOGE("%p is not a gralloc handle", buffer);
		return mapper_error::BAD_BUFFER;
	}

	return get_metadata(private_handle, metadata, output, encoder);
}

mapper_error set(const void *buffer, const metadata_descriptor &metadata, const uint8_t *data, size_t data_size,
                 metadata_decoder decoder)
{
	auto handle = handle_cast<imported_handle>(static_cast<buffer_handle_t>(buffer));
	if (metadata.is_aml_metadata_type()) {
		/* The buffer must have been allocated by Gralloc */
		buffer_handle_t importHnd = RegisteredHandlePool::get_instance().get(buffer);
		if (importHnd == nullptr)
		{
			MALI_GRALLOC_LOGV("%s fallback to check again", __FUNCTION__);
			/* fallback to check if exists a bufhandle's fd is the same with input handle*/
			importHnd = RegisteredHandlePool::get_instance().aml_get(buffer);
			if (importHnd == nullptr)
			{
				AML_GRALLOC_LOGI("%s-> Buffer: %p has not been registered with Gralloc", __FUNCTION__, buffer);
				return mapper_error::BAD_BUFFER;
			}
		}
		handle = handle_cast<imported_handle>(importHnd);
	}

	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("set: %p has not been imported", buffer);
		return mapper_error::BAD_BUFFER;
	}
	return set_metadata(handle, metadata, data, data_size, decoder);
}

mapper_error get_reserved_region(const void *buffer, void **out_reserved_region, uint64_t &out_reserved_region_size)
{
	auto handle = handle_cast<imported_handle>(static_cast<buffer_handle_t>(buffer));
	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("getReservedRegion: %p has not been imported", buffer);
		return mapper_error::BAD_BUFFER;
	}
	else if (handle->reserved_region_size == 0)
	{
		MALI_GRALLOC_LOGE("Buffer: %p has no reserved region", buffer);
		return mapper_error::BAD_BUFFER;
	}

	*out_reserved_region = static_cast<std::byte *>(handle->attr_base) + mapper::common::shared_metadata_size();
	out_reserved_region_size = handle->reserved_region_size;
	return mapper_error::NONE;
}

mapper_error get_transport_size(const void *buffer, int &out_num_fds, int &out_num_ints)
{
	auto handle = handle_cast<imported_handle>(static_cast<buffer_handle_t>(buffer));
	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("Buffer %p is not registered with Gralloc", buffer);
		return mapper_error::BAD_BUFFER;
	}

	assert(handle->numInts == PRIVATE_HANDLE_NUM_INTS);

	out_num_fds = handle->numFds;
	out_num_ints = handle->numInts;
	return mapper_error::NONE;
}

const std::vector<metadata_type> &list_supported_metadata_types()
{
	return metadata_descriptions;
}

static buffer_dump dump_buffer_helper(const private_handle_t *handle,
                                      const std::unordered_map<StandardMetadataType, metadata_encoder> &encoders,
                                      bool isMapperV5)
{
	std::vector<metadata_dump> out;
	const int max_required_size = 512;
	for (const auto &it : metadata_descriptions)
	{
		if (!it.m_descriptor.is_standard_metadata_type())
		{
			continue;
		}
		else if (handle->type == handle_type::raw && it.m_is_settable)
		{
			/* We can only dump mutable metadata for imported buffers. */
			continue;
		}

		auto encoder = encoders.find(it.m_descriptor.get_standard_metadata_type_value());
		if (encoder == encoders.end())
		{
			/* Can't encode the metadata */
			continue;
		}

		std::vector<uint8_t> out_buffer;
		if (isMapperV5)
		{
			out_buffer.resize(max_required_size + sizeof(mapper_data));
			mapper_data *data = reinterpret_cast<mapper_data *>(out_buffer.data());
			void *outData = reinterpret_cast<void *>(data + 1);
			*data = { outData, max_required_size, 0 };
		}

		auto err = get_metadata(handle, it.m_descriptor, out_buffer, encoder->second);
		if (err == mapper_error::NONE)
		{
			if (isMapperV5)
			{
				if (out_buffer.size() > sizeof(mapper_data))
				{
					std::vector<uint8_t> meta_data((out_buffer.begin() + sizeof(mapper_data)), out_buffer.end());
					out.push_back({ it.m_descriptor, std::move(meta_data) });
				}
			}
			else
			{
				out.push_back({ it.m_descriptor, std::move(out_buffer) });
			}
		}
	}
	return buffer_dump{ std::move(out) };
}

mapper_error dump_buffer(const void *buffer, buffer_dump &out_buffer_dump,
						 const std::unordered_map<StandardMetadataType, metadata_encoder> &encoders,
						 bool isMapperV5)
{
	/* Note: handles passed to dumpBuffer may be raw or imported. */
	auto handle = handle_cast<private_handle_t>(static_cast<const native_handle *>(buffer));
	if (handle == nullptr)
	{
		MALI_GRALLOC_LOGE("dumpBuffer: %p is not a gralloc buffer handle", buffer);
		return mapper_error::BAD_BUFFER;
	}

	out_buffer_dump = dump_buffer_helper(handle, encoders, isMapperV5);
	return mapper_error::NONE;
}

mapper_error dump_buffers(std::vector<buffer_dump> &out_buffer_dumps,
						  const std::unordered_map<StandardMetadataType, metadata_encoder> &encoders,
						  bool isMapperV5)
{
	std::vector<buffer_dump> buffer_dumps;
	RegisteredHandlePool::get_instance().for_each([&buffer_dumps, &encoders, &isMapperV5](buffer_handle_t buffer) {
		auto handle = handle_cast<imported_handle>(buffer);
		if (handle == nullptr)
		{
			MALI_GRALLOC_LOGW("dumpBuffer: %p has been unimported by a different IMapper instance. Skipping.", buffer);
			return;
		}

		buffer_dump buffer_dump{ dump_buffer_helper(handle, encoders, isMapperV5) };
		buffer_dumps.push_back(std::move(buffer_dump));
	});

	out_buffer_dumps = std::move(buffer_dumps);
	return mapper_error::NONE;
}

static std::vector<std::vector<PlaneLayoutComponent>> plane_layout_components_from_handle(const private_handle_t *hnd)
{
	/* Re-define the component constants to make the table easier to read. */
	const ExtendableType R = android::gralloc4::PlaneLayoutComponentType_R;
	const ExtendableType G = android::gralloc4::PlaneLayoutComponentType_G;
	const ExtendableType B = android::gralloc4::PlaneLayoutComponentType_B;
	const ExtendableType A = android::gralloc4::PlaneLayoutComponentType_A;
	const ExtendableType CB = android::gralloc4::PlaneLayoutComponentType_CB;
	const ExtendableType CR = android::gralloc4::PlaneLayoutComponentType_CR;
	const ExtendableType Y = android::gralloc4::PlaneLayoutComponentType_Y;
	const ExtendableType RAW = android::gralloc4::PlaneLayoutComponentType_RAW;

	struct table_entry
	{
		uint32_t drm_fourcc;
		std::vector<std::vector<PlaneLayoutComponent>> components;
	};

	/* clang-format off */
	static table_entry table[] = {
		/* 16 bit RGB(A) */
		{
			.drm_fourcc = DRM_FORMAT_RGB565,
			.components = { { { B, 0, 5 }, { G, 5, 6 }, { R, 11, 5 } } }
		},
		{
			.drm_fourcc = DRM_FORMAT_BGR565,
			.components = { { { R, 0, 5 }, { G, 5, 6 }, { B, 11, 5 } } }
		},
		/* 24 bit RGB(A) */
		{
			.drm_fourcc = DRM_FORMAT_BGR888,
			.components = { { { R, 0, 8 }, { G, 8, 8 }, { B, 16, 8 } } }
		},
		/* 32 bit RGB(A) */
		{
			.drm_fourcc = DRM_FORMAT_ARGB8888,
			.components = { { { B, 0, 8 }, { G, 8, 8 }, { R, 16, 8 }, { A, 24, 8 } } }
		},
		{
			.drm_fourcc = DRM_FORMAT_ABGR8888,
			.components = { { { R, 0, 8 }, { G, 8, 8 }, { B, 16, 8 }, { A, 24, 8 } } }
		},
		{
			.drm_fourcc = DRM_FORMAT_XBGR8888,
			.components = { { { R, 0, 8 }, { G, 8, 8 }, { B, 16, 8 } } }
		},
		{
			.drm_fourcc = DRM_FORMAT_ABGR2101010,
			.components = { { { R, 0, 10 }, { G, 10, 10 }, { B, 20, 10 }, { A, 30, 2 } } }
		},
		/* 64 bit RGB(A) */
		{
			.drm_fourcc = DRM_FORMAT_ABGR16161616F,
			.components = { { { R, 0, 16 }, { G, 16, 16 }, { B, 32, 16 }, { A, 48, 16 } } }
		},
		/* 10 bit packed RGBA */
		{
			.drm_fourcc = DRM_FORMAT_AXBXGXRX106106106106,
			.components = { { { R, 6, 10 }, { G, 22, 10 }, { B, 38, 10 }, { A, 54, 10 } } }
		},
		/* Single plane 8 bit YUV 4:2:2 */
		{
			.drm_fourcc = DRM_FORMAT_YUYV,
			.components = { { { Y, 0, 8 }, { CB, 8, 8 }, { Y, 16, 8 }, { CR, 24, 8 } } }
		},
		/* Single plane 10 bit YUV 4:4:4 */
		{
			.drm_fourcc = DRM_FORMAT_Y410,
			.components = { { { CB, 0, 10 }, { Y, 10, 10 }, { CR, 20, 10 }, { A, 30, 2 } } }
		},
		/* Single plane 10 bit YUV 4:2:2 */
		{
			.drm_fourcc = DRM_FORMAT_Y210,
			.components = { { { Y, 6, 10 }, { CB, 22, 10 }, { Y, 38, 10 }, { CR, 54, 10 } } }
		},
		/* Single plane 10 bit YUV 4:2:0 */
		{
			.drm_fourcc = DRM_FORMAT_Y0L2,
			.components = { {
				{ Y, 0, 10 }, { CB, 10, 10 }, { Y, 20, 10 }, { A, 30, 1 }, { A, 31, 1 },
				{ Y, 32, 10 }, { CR, 42, 10 }, { Y, 52, 10 }, { A, 62, 1 }, { A, 63, 1 }
			} }
		},
		/* Semi-planar 8 bit YUV 4:2:2 */
		{
			.drm_fourcc = DRM_FORMAT_NV16,
			.components = {
				{ { Y, 0, 8 } },
				{ { CB, 0, 8 }, { CR, 8, 8 } }
			}
		},
		/* Semi-planar 8 bit YUV 4:2:0 */
		{
			.drm_fourcc = DRM_FORMAT_NV12,
			.components = {
				{ { Y, 0, 8 } },
				{ { CB, 0, 8 }, { CR, 8, 8 } }
			}
		},
		{
			.drm_fourcc = DRM_FORMAT_NV21,
			.components = {
				{ { Y, 0, 8 } },
				{ { CR, 0, 8 }, { CB, 8, 8 } }
			}
		},
		/* Semi-planar 10 bit YUV 4:2:2 */
		{
			.drm_fourcc = DRM_FORMAT_P210,
			.components = {
				{ { Y, 6, 10 } },
				{ { CB, 6, 10 }, { CR, 22, 10 } }
			}
		},
		/* Semi-planar 10 bit YUV 4:2:0 */
		{
			.drm_fourcc = DRM_FORMAT_P010,
			.components = {
				{ { Y, 6, 10 } },
				{ { CB, 6, 10 }, { CR, 22, 10 } }
			}
		},
		/* Planar 8 bit YVU 4:2:0 */
		{
			.drm_fourcc = DRM_FORMAT_YVU420,
			.components = {
				{ { Y, 0, 8 } },
				{ { CR, 0, 8 } },
				{ { CB, 0, 8 } }
			}
		},
		/* Planar 8 bit YUV 4:2:0 */
		{
			.drm_fourcc = DRM_FORMAT_YUV420,
			.components = {
				{ { Y, 0, 8 } },
				{ { CB, 0, 8 } },
				{ { CR, 0, 8 } }
			}
		},
		/* Planar 8 bit YUV 4:4:4 */
		{
			.drm_fourcc = DRM_FORMAT_YUV444,
			.components = {
				{ { Y, 0, 8 } },
				{ { CB, 0, 8 } },
				{ { CR, 0, 8 } }
			}
		},
		/* AFBC Only FourCC */
		{.drm_fourcc = DRM_FORMAT_YUV420_8BIT, .components = { {} } },
		{.drm_fourcc = DRM_FORMAT_YUV420_10BIT, .components = { {} } },
		/* 8 Bit R Channel */
		{
			.drm_fourcc = DRM_FORMAT_R8,
			.components = { { {R, 0, 8} } },
		},
	};
	/* clang-format on */

	/* Special case for formats that can't be represented by a DRM fourcc */
	const auto internal_format = hnd->alloc_format;
	if (!internal_format.has_modifiers())
	{
		switch (internal_format.get_base())
		{
		case MALI_GRALLOC_FORMAT_INTERNAL_RAW10:
		case MALI_GRALLOC_FORMAT_INTERNAL_RAW12:
			std::vector<std::vector<PlaneLayoutComponent>> components = { { { RAW, 0, -1 } } };
			return components;
		}
	}

	const uint32_t drm_fourcc = drm_fourcc_from_handle(hnd);
	if (drm_fourcc != DRM_FORMAT_INVALID)
	{
		for (const auto &entry : table)
		{
			if (entry.drm_fourcc == drm_fourcc)
			{
				return entry.components;
			}
		}
	}

	MALI_GRALLOC_LOGW("Could not find component description for FourCC value %x", drm_fourcc);
	return std::vector<std::vector<PlaneLayoutComponent>>(0);
}

mapper_error get_plane_layouts(const private_handle_t *handle, std::vector<PlaneLayout> *layouts)
{
	const int num_planes = handle->get_num_planes();
	const auto internal_format = handle->alloc_format;
	const format_info_t *format_info = internal_format.get_base_info();
	if (format_info == nullptr)
	{
		MALI_GRALLOC_LOGE("Invalid format in get_plane_layouts");
		return mapper_error::BAD_VALUE;
	}
	std::vector<std::vector<PlaneLayoutComponent>> components = plane_layout_components_from_handle(handle);
	layouts->reserve(num_planes);
	for (size_t plane_index = 0; plane_index < num_planes; ++plane_index)
	{
		int64_t plane_size;
		if (plane_index < num_planes - 1)
		{
			plane_size = handle->plane_info[plane_index + 1].offset;
		}
		else
		{
			int64_t layer_size = handle->size / handle->layer_count;
			plane_size = layer_size - handle->plane_info[plane_index].offset;
		}

		bool is_raw = false;
		switch (internal_format.get_base())
		{
		case MALI_GRALLOC_FORMAT_INTERNAL_RAW10:
		case MALI_GRALLOC_FORMAT_INTERNAL_RAW12:
			is_raw = true;
			break;
		}

		int64_t sample_increment_in_bits = 0;
		if (internal_format.has_modifiers() || !is_raw)
		{
			sample_increment_in_bits =
			    (internal_format.is_afbc()) ? format_info->bpp_afbc[plane_index] : format_info->bpp[plane_index];
		}

		PlaneLayout layout = {
			.components =
			    components.size() > plane_index ? components[plane_index] : std::vector<PlaneLayoutComponent>(0),
			.offsetInBytes = handle->plane_info[plane_index].offset,
			.sampleIncrementInBits = sample_increment_in_bits,
			.strideInBytes = handle->plane_info[plane_index].byte_stride,
			.widthInSamples = handle->plane_info[plane_index].alloc_width,
			.heightInSamples = handle->plane_info[plane_index].alloc_height,
			.totalSizeInBytes = plane_size,
			.horizontalSubsampling = (plane_index == 0 ? 1 : format_info->hsub),
			.verticalSubsampling = (plane_index == 0 ? 1 : format_info->vsub),
		};
		layouts->push_back(layout);
	}

	return mapper_error::NONE;
}

} // namespace common
} // namespace mapper
} // namespace arm
