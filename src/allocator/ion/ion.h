/*
 * Copyright (C) 2016, 2018-2020 ARM Limited. All rights reserved.
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

#ifndef MALI_GRALLOC_ION_H_
#define MALI_GRALLOC_ION_H_

#define SZ_4K				0x00001000

#ifdef GRALLOC_AML_EXTEND
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

#define UVM_IOC_MAGIC 'U'
#define UVM_IOC_ALLOC _IOWR(UVM_IOC_MAGIC, 0, \
				struct uvm_alloc_data)

#define UVM_IOC_FREE _IOWR(UVM_IOC_MAGIC, 1, \
				struct uvm_alloc_data)
#endif

#endif /* MALI_GRALLOC_ION_H_ */
