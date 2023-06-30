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
