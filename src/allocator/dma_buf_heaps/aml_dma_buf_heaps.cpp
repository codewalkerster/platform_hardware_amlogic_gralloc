/*
 * Copyright (C) 2022 Arm Limited. All rights reserved.
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

#include <vector>
#include <inttypes.h>
#include <BufferAllocator/BufferAllocator.h>
#include <android-base/unique_fd.h>

#include "allocator/allocator.h"
#include "core/buffer_allocation.h"
#include "core/buffer_descriptor.h"
#include "usages.h"
#include <am_gralloc_internal.h>
#include <cutils/properties.h>
#include "aml_dma_buf_heaps.h"

#define V4L2_DECODER_BUFFER_MAX_WIDTH       4096
#define V4L2_DECODER_BUFFER_MAX_HEIGHT      2304
#define V4L2_DECODER_BUFFER_8k_MAX_WIDTH       8192
#define V4L2_DECODER_BUFFER_8k_MAX_HEIGHT      4352

struct uvm_ext_device
{
    static void close_uvm()
    {
        uvm_ext_device &dev = get_inst();
        if (dev.uvm_dev >= 0)
        {
            ::close(dev.uvm_dev);
            dev.uvm_dev = -1;
        }
    }

    static int get_uvm()
    {
        uvm_ext_device &dev = get_inst();
        if (dev.uvm_dev < 0)
        {
            dev.uvm_dev = open("/dev/uvm", O_RDONLY | O_CLOEXEC);
        }

        if (dev.uvm_dev < 0)
        {
            return -1;
        }
        return dev.uvm_dev;
    }

private:
	int uvm_dev;
	uvm_ext_device()
	    :uvm_dev(-1)
	{
	}
	~uvm_ext_device()
	{
		close_uvm();
	}

	static uvm_ext_device& get_inst()
	{
		static uvm_ext_device dev;
		return dev;
	}
};

enum class dma_buf_heap
{
	/* Upstream heaps */
	system,
	system_uncached,

	/* Custom heaps */
	protected_memory,
	physically_contiguous_gfx,
	physically_contiguous_fb,
	physically_contiguous_codec_mm,
};

struct custom_heap
{
	const char *name;
	struct
	{
		const char *name;
		int flags;
	}
	ion_fallback;
};

const custom_heap physically_contiguous_gfx_heap =
{
	"heap-gfx",
	{
		"ion-dev",
		0,
	},
};

const custom_heap physically_contiguous_fb_heap =
{
	"heap-fb",
	{
		"ion-fb",
		0,
	},
};

const custom_heap physically_contiguous_codec_mm_heap =
{
	"heap-cached-codecmm",
	{
		"codec_mm_cma",
		0,
	},
};


const custom_heap protected_memory_heap =
{
	"heap-secure",
	{
		"ion_secure",
		0,
	},
};

const custom_heap custom_heaps[] =
{
	physically_contiguous_gfx_heap,
	physically_contiguous_fb_heap,
	physically_contiguous_codec_mm_heap,
	protected_memory_heap,
};

enum dma_buf_heap am_gralloc_pick_dma_buf_heap(
					const buffer_descriptor_t *descriptor,
					uint64_t usage);
static int am_gralloc_exec_omx_policy(
					int size, int scalar,
					const buffer_descriptor_t *descriptor);
static int am_gralloc_exec_uvm_policy(
					const buffer_descriptor_t *bufDescriptor,
					uint64_t usage,
					struct uvm_exec_data *agu);

static const char *get_dma_buf_heap_name(dma_buf_heap heap)
{
	switch (heap)
	{
	case dma_buf_heap::system:
		return kDmabufSystemHeapName;
	case dma_buf_heap::system_uncached:
		return kDmabufSystemUncachedHeapName;
	case dma_buf_heap::protected_memory:
		return protected_memory_heap.name;
	case dma_buf_heap::physically_contiguous_gfx:
		return physically_contiguous_gfx_heap.name;
	case dma_buf_heap::physically_contiguous_fb:
		return physically_contiguous_fb_heap.name;
	case dma_buf_heap::physically_contiguous_codec_mm:
		return physically_contiguous_codec_mm_heap.name;
	}
}

static BufferAllocator *get_global_buffer_allocator()
{
	static struct allocator_initialization
	{
		BufferAllocator allocator;
		allocator_initialization()
		{
			for (const auto &heap : custom_heaps)
			{
				allocator.MapNameToIonHeap(heap.name, heap.ion_fallback.name, heap.ion_fallback.flags);
			}
		}
	}
	instance;

	return &instance.allocator;
}

static dma_buf_heap pick_dma_buf_heap(uint64_t usage)
{
	if (usage & GRALLOC_USAGE_PROTECTED)
	{
		return dma_buf_heap::protected_memory;
	}
	else if (!(usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) && (usage & GRALLOC_USAGE_HW_FB))
	{
#if defined(GRALLOC_USE_CONTIGUOUS_DISPLAY_MEMORY) && GRALLOC_USE_CONTIGUOUS_DISPLAY_MEMORY
		return dma_buf_heap::physically_contiguous;
#else
		return dma_buf_heap::system;
#endif
	}
	else if ((usage & GRALLOC_USAGE_SW_READ_MASK) == GRALLOC_USAGE_SW_READ_OFTEN)
	{
		return dma_buf_heap::system;
	}
	else
	{
		return dma_buf_heap::system_uncached;
	}
}

int allocator_allocate(const buffer_descriptor_t *descriptor, private_handle_t **out_handle)
{
	auto allocator = get_global_buffer_allocator();

	uint64_t usage = descriptor->consumer_usage | descriptor->producer_usage;
	auto heap = am_gralloc_pick_dma_buf_heap(descriptor, usage);
	auto heap_name = get_dma_buf_heap_name(heap);

	struct uvm_exec_data *agu = (struct uvm_exec_data *)malloc(sizeof(uvm_exec_data));
	int shared_fd = am_gralloc_exec_uvm_policy(descriptor, usage, agu);

#ifdef AML_GRALLOC_DEBUG
	AML_GRALLOC_LOGI("shared_fd: (%d) agu->delay_alloc:%d agu->uvm_flag:%d",
					shared_fd, agu->delay_alloc, agu->uvm_flag);
#endif

	if (shared_fd < 0) {
		if (agu->uvm_buffer_flag) {
			MALI_GRALLOC_LOGE("Failed to allocate from codec_mm!");
			free(agu);
			return -ENOMEM;
		}

		shared_fd = allocator->Alloc(heap_name, descriptor->size);
#ifdef AML_GRALLOC_DEBUG
		AML_GRALLOC_LOGI("not video buffer, allocate from heap: %s fd:%d", heap_name, shared_fd);
#endif
		agu->delay_alloc = 0;
	}



	if (shared_fd < 0)
	{
		if (heap == dma_buf_heap::system ||
			heap == dma_buf_heap::protected_memory ||
			heap == dma_buf_heap::physically_contiguous_fb)
		{
			MALI_GRALLOC_LOGE("libdmabufheap allocation failed for %s heap", heap_name);
			free(agu);
			return -ENOMEM;
		}
		else
		{
			MALI_GRALLOC_LOGW("libdmabufheap allocation failed for %s heap, falling back to system heap", heap_name);
			shared_fd = allocator->Alloc(get_dma_buf_heap_name(dma_buf_heap::system), descriptor->size);
			if (shared_fd < 0)
			{
				MALI_GRALLOC_LOGE("libdmabufheap fallback allocation failed");
				free(agu);
				return -ENOMEM;
			}
		}
	}
	android::base::unique_fd fd(shared_fd);
	*out_handle = make_private_handle(
	    agu->uvm_buffer_flag, descriptor->size,
	    descriptor->consumer_usage, descriptor->producer_usage, std::move(fd), descriptor->hal_format,
	    descriptor->alloc_format, descriptor->width, descriptor->height, descriptor->size, descriptor->layer_count,
	    descriptor->plane_info, descriptor->pixel_stride);
#ifdef AML_GRALLOC_DEBUG
	AML_GRALLOC_LOGI("%s: heap_name:%s width:%d height:%d stride:%d format=0x%" PRIx64 " usage=0x%" PRIx64,
			__FUNCTION__, heap_name, descriptor->width, descriptor->height, descriptor->pixel_stride,
			descriptor->hal_format, usage);
#endif

	if (nullptr == *out_handle)
	{
		MALI_GRALLOC_LOGE("Private handle could not be created for descriptor");
		free(agu);
		return -ENOMEM;
	}

	(*out_handle)->ion_delay_alloc = agu->delay_alloc;
	(*out_handle)->am_extend_fd = ::dup((*out_handle)->share_fd);
	(*out_handle)->am_extend_type = 0;
	(*out_handle)->req_width = descriptor->width;
	(*out_handle)->req_height = descriptor->height;
	(*out_handle)->format = descriptor->hal_format;
	(*out_handle)->usage = usage;

	free(agu);

	return 0;
}

void allocator_free(private_handle_t *handle)
{
	if (handle == nullptr)
	{
		return;
	}

	if (handle->base != nullptr)
	{
		munmap(handle->base, handle->size);
	}

	close(handle->share_fd);
	handle->share_fd = -1;
}

static SyncType make_sync_type(bool read, bool write)
{
	if (read && write)
	{
		return kSyncReadWrite;
	}
	else if (read)
	{
		return kSyncRead;
	}
	else if (write)
	{
		return kSyncWrite;
	}
	else
	{
		return static_cast<SyncType>(0);
	}
}

int allocator_sync_start(const private_handle_t *handle, bool read, bool write)
{
	auto allocator = get_global_buffer_allocator();
	return allocator->CpuSyncStart(static_cast<unsigned>(handle->share_fd), make_sync_type(read, write));
}

int allocator_sync_end(const private_handle_t *handle, bool read, bool write)
{
	auto allocator = get_global_buffer_allocator();
	return allocator->CpuSyncEnd(static_cast<unsigned>(handle->share_fd), make_sync_type(read, write));
}

int allocator_map(private_handle_t *handle)
{
	void *hint = nullptr;
	int protection = PROT_READ | PROT_WRITE, flags = MAP_SHARED;
	off_t page_offset = 0;

	if (handle->ion_delay_alloc)
		return 0;
	uint64_t usage = handle->producer_usage | handle->consumer_usage;
	if (am_gralloc_is_video_decoder_quarter_buffer_usage(usage) ||
		am_gralloc_is_video_decoder_one_sixteenth_buffer_usage(usage)) {
		return 0;
	}
	void *mapping = mmap(hint, handle->size, protection, flags, handle->share_fd, page_offset);
	if (MAP_FAILED  == mapping)
	{
		MALI_GRALLOC_LOGE("mmap(share_fd = %d) failed: %s", handle->share_fd, strerror(errno));
		return -errno;
	}

	handle->base = static_cast<std::byte *>(mapping);

	return 0;
}

void allocator_unmap(private_handle_t *handle)
{
	void *base = static_cast<std::byte *>(handle->base);
	uint64_t usage = handle->producer_usage | handle->consumer_usage;
	if (handle->ion_delay_alloc ||
		am_gralloc_is_video_decoder_quarter_buffer_usage(usage) ||
		am_gralloc_is_video_decoder_one_sixteenth_buffer_usage(usage)) {
		return;
	}
	if (munmap(base, handle->size) < 0)
	{
		MALI_GRALLOC_LOGE("munmap(base = %p, size = %d) failed: %s", base, handle->size, strerror(errno));
	}

	handle->base = nullptr;
	handle->cpu_write = false;
	handle->lock_count = 0;
}

void allocator_close()
{
	/* nop */
}

bool is_android_yuv_format(int req_format)
{
	bool rval = false;

	switch (req_format)
	{
	case HAL_PIXEL_FORMAT_YV12:
	case HAL_PIXEL_FORMAT_Y8:
	case HAL_PIXEL_FORMAT_Y16:
	case HAL_PIXEL_FORMAT_YCbCr_420_888:
	case HAL_PIXEL_FORMAT_YCbCr_422_888:
	case HAL_PIXEL_FORMAT_YCbCr_444_888:
	case HAL_PIXEL_FORMAT_YCrCb_420_SP:
	case HAL_PIXEL_FORMAT_YCbCr_422_SP:
	case HAL_PIXEL_FORMAT_YCBCR_422_I:
	case HAL_PIXEL_FORMAT_RAW16:
	case HAL_PIXEL_FORMAT_RAW12:
	case HAL_PIXEL_FORMAT_RAW10:
	case HAL_PIXEL_FORMAT_RAW_OPAQUE:
	case HAL_PIXEL_FORMAT_BLOB:
	case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
		rval = true;
		break;
	}

	return rval;
}

static int am_gralloc_exec_omx_policy(
	int size, int scalar,
	const buffer_descriptor_t *max_bufDescriptor) {

	char prop[PROPERTY_VALUE_MAX];
	/*
	 * support for 8k video
	 * Set max 8k size if bigger then 4k
	 */
	if ((max_bufDescriptor->width * max_bufDescriptor->height) >
			(V4L2_DECODER_BUFFER_MAX_WIDTH * V4L2_DECODER_BUFFER_MAX_HEIGHT))
		size = V4L2_DECODER_BUFFER_8k_MAX_WIDTH * V4L2_DECODER_BUFFER_8k_MAX_HEIGHT * 3 / 2;

	size /= scalar * scalar;

	/*
	 * workaround for unsupported 4k video play on some platforms
	 */
	if (property_get("ro.vendor.platform.support.4k", prop, NULL) > 0) {
		if (strstr(prop, "false"))
			size = (GRALLOC_ALIGN(1920/scalar, 64) * GRALLOC_ALIGN(1080/scalar, 64)) * 3 / 2;
	}
	/*
	 * workaround to alloc fixed 1080p buffer for 1/16 usage
	 * if vendor.media.omx2.1080p_buffer is true
	 */
	if (scalar == 4 &&
		property_get("vendor.media.omx2.1080p_buffer", prop, NULL) > 0) {
		if (strstr(prop, "true")) {
			size = (GRALLOC_ALIGN(1920, 64) * GRALLOC_ALIGN(1080, 64)) * 3 / 2;
			MALI_GRALLOC_LOGW("[gralloc]: allocate fixed 1080p buffer for 1/16 usage size:%d", size);
		}
	}
	return size;
}

static int am_gralloc_exec_uvm_policy(
					const buffer_descriptor_t *bufDescriptor,
					uint64_t usage,
					struct uvm_exec_data *agu) {
	int uvm_fd = -1;
	int ret = -1;
	int buf_scalar = 1;
	int aligned_bit = 1;
	int v4l2_dec_max_buf_size =
		(V4L2_DECODER_BUFFER_MAX_WIDTH * V4L2_DECODER_BUFFER_MAX_HEIGHT) * 3 / 2;

	uvm_fd = uvm_ext_device::get_uvm();
	if (uvm_fd < 0) {
		MALI_GRALLOC_LOGE("%s, get_uvm failed uvm_fd=%d",
				__func__, uvm_fd);
		return ret;
	}

	agu->delay_alloc = 0;
	agu->uvm_flag = UVM_IMM_ALLOC;
	agu->uvm_buffer_flag = 0;

	if (am_gralloc_is_omx_metadata_extend_usage(usage) ||
		am_gralloc_is_omx_osd_extend_usage(usage)) {

		agu->uvm_buffer_flag |= private_handle_t::PRIV_FLAGS_UVM_BUFFER;

		if (am_gralloc_is_omx_osd_extend_usage(usage) ||
			am_gralloc_is_video_decoder_full_buffer_usage(usage) ||
			am_gralloc_is_video_decoder_OSD_buffer_usage(usage)) {
			buf_scalar = 1;
		} else if (am_gralloc_is_video_decoder_quarter_buffer_usage(usage)) {
			buf_scalar = 2;
		} else if (am_gralloc_is_video_decoder_one_sixteenth_buffer_usage(usage)) {
			buf_scalar = 4;
		} else {
			agu->uvm_flag = UVM_DELAY_ALLOC;
			agu->delay_alloc = 1;
		}

		if (usage & GRALLOC_USAGE_PROTECTED)
			agu->uvm_flag |= UVM_USAGE_PROTECTED;

		if ((usage & GRALLOC_USAGE_SW_READ_MASK) == GRALLOC_USAGE_SW_READ_OFTEN)
			agu->uvm_flag |= UVM_USAGE_CACHED;

		if (usage & GRALLOC_USAGE_PRIVATE_3)
			agu->uvm_flag |= UVM_FBC_DEC;

		if (am_gralloc_is_video_decoder_OSD_buffer_usage(usage))
			agu->uvm_flag |= UVM_SKIP_REALLOC;

		if (need_do_width_height_align(usage, bufDescriptor->width, bufDescriptor->height))
			aligned_bit = 64;

		v4l2_dec_max_buf_size = am_gralloc_exec_omx_policy(
									v4l2_dec_max_buf_size,
									buf_scalar,
									bufDescriptor);

		struct uvm_alloc_data uad = {
			.size = (int)bufDescriptor->size,
			.byte_stride = (int)bufDescriptor->plane_info[0].byte_stride,
			.width = bufDescriptor->width,
			.height = bufDescriptor->height,
			.align = aligned_bit,
			.flags = agu->uvm_flag,
			.scalar = buf_scalar,
			.scaled_buf_size = v4l2_dec_max_buf_size
		};
		ret = ioctl(uvm_fd, UVM_IOC_ALLOC, &uad);
		if (ret < 0) {
			MALI_GRALLOC_LOGE("%s, ioctl::UVM_IOC_ALLOC failed uvm_fd=%d",
				__func__, uvm_fd);
			return ret;
		}

		return uad.fd;
	}
	return ret;
}

enum dma_buf_heap am_gralloc_pick_dma_buf_heap(
	const buffer_descriptor_t *descriptor, uint64_t usage)
{
	if (usage & GRALLOC_USAGE_PROTECTED)
	{
		return dma_buf_heap::protected_memory;
	}

	if (usage & GRALLOC_USAGE_HW_FB)
	{
		return dma_buf_heap::physically_contiguous_fb;
	}

	if (am_gralloc_is_omx_osd_extend_usage(usage) ||
		(usage & GRALLOC_USAGE_HW_CAMERA_WRITE) ||
		(usage & GRALLOC_USAGE_HW_VIDEO_ENCODER))
	{
		return dma_buf_heap::physically_contiguous_codec_mm;
	}

	if (usage & GRALLOC_USAGE_HW_COMPOSER)
	{
		if (!is_android_yuv_format(descriptor->hal_format))
		{
			return dma_buf_heap::physically_contiguous_gfx;
		}
	}

	if ((usage & GRALLOC_USAGE_SW_READ_MASK) == GRALLOC_USAGE_SW_READ_OFTEN ||
		am_gralloc_is_omx_metadata_extend_usage(usage))
	{
		return dma_buf_heap::system;
	}
	else
	{
		return dma_buf_heap::system_uncached;
	}

	return dma_buf_heap::system;
}
