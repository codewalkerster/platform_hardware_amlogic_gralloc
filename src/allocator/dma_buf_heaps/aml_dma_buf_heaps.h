/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */
#pragma once

#define GRALLOC_ALIGN(value, base) ((((value) + (base) -1) / (base)) * (base))

struct uvm_alloc_data {
	int size;
	int align;
	unsigned int flags;
	int v4l2_fd;
	int fd;
	int byte_stride;
	uint32_t width;
	uint32_t height;
	int scalar;
	int scaled_buf_size;
};
struct uvm_exec_data {
	uint32_t delay_alloc;
	uint32_t uvm_flag;
/* judge if it is allocated from UVM */
	uint32_t uvm_buffer_flag;
};

#ifdef ALLOC_FROM_SAME_HEAP
struct meson_cma_heap_info {
	char     heap_name[32];
	uint64_t num_of_buffers;
	uint64_t num_of_alloc_bytes;
	uint64_t num_of_free_bytes;
};

#define MESON_CMA_HEAP_IOC_MAGIC 'M'
#define MESON_CMA_HEAP_IOC_GET_INFO _IOWR(MESON_CMA_HEAP_IOC_MAGIC, 0, \
				struct meson_cma_heap_info)

struct aml_gralloc_layer_info {
	int count;            /* Remaining unallocated buffer(s) count of the current layer */
	size_t size;          /* size of single buffer */
	uint64_t time_stamp;  /* Used to record the timestamp of the last buffer allocation for this layer.
	                       * In certain abnormal situations or when the scenario is not fully considered,
	                       * there may be situations where there are not enough applications. If not verified,
	                       * it will cause the layer to remain in a pending allocation state and not be released,
	                       * resulting in abnormal table resources.
	                       */
	bool pick_heap_gfx;   /* Record whether the first buffer of a certain layer is allocated from heap gfx.
	                       * Used to avoid the following scenario: when the layer applies for the first buffer,
	                       * the heap gfx resources are insufficient and allocated from the system. When applying
	                       * for the second buffer, the resources are sufficient again, resulting in the three
	                       * buffers of the layer being allocated from different heaps.
	                       */
};
#endif

#define UVM_IMM_ALLOC        (1 << 0)
#define UVM_DELAY_ALLOC      (1 << 1)
#define UVM_FAKE_ALLOC       (1 << 2)
#define UVM_USAGE_PROTECTED  (1 << 3)
#define UVM_SKIP_REALLOC     (1 << 4)
#define UVM_USAGE_CACHED     (1 << 5)
#define UVM_FBC_DEC          (1 << 6)
#define UVM_SIZE_SKIP        (1 << 7)



#define UVM_IOC_MAGIC 'U'
#define UVM_IOC_ALLOC _IOWR(UVM_IOC_MAGIC, 0, \
				struct uvm_alloc_data)

#define UVM_IOC_FREE _IOWR(UVM_IOC_MAGIC, 1, \
				struct uvm_alloc_data)
