/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef AM_GRALLOC_EXT_H
#define AM_GRALLOC_EXT_H

#include <hardware/gralloc1.h>
#include <utils/NativeHandle.h>
#include <buffer.h>

/*
For ioctl uvm operation
*/
struct uvm_usage_data {
	int fd;
	int usage;
};

#define UVM_IOC_MAGIC 'U'
#define UVM_IOC_SET_USAGE _IOWR(UVM_IOC_MAGIC, 9, \
				struct uvm_usage_data)

#define UVM_IOC_GET_USAGE _IOWR(UVM_IOC_MAGIC, 10, \
				struct uvm_usage_data)

/*for userspace to call uvm ioctl to add buf usage*/
bool am_gralloc_get_uvm_buf_usage(const native_handle_t * hnd, int *usage);
bool am_gralloc_set_uvm_buf_usage(const native_handle_t * hnd, int usage);

#endif
