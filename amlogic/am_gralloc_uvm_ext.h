/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#ifndef AM_GRALLOC_UVM_EXT_H
#define AM_GRALLOC_UVM_EXT_H

#include <hardware/gralloc1.h>
#include <utils/NativeHandle.h>
#include <core/buffer.h>
#include <sstream>

/*
For ioctl uvm operation
*/
struct uvm_usage_data {
	int fd;
	int usage;
};

struct uvm_decoder_para {
	uint32_t slot_id{~0U};
	uint32_t width{0};
	uint32_t height{0};
	uint32_t w_align{0};
	uint32_t h_align{0};
	uint32_t size{0};
	uint32_t compress{0};
	std::string to_str()
	{
		std::stringstream ss;
		ss << "[slot_id=" << slot_id
		   << ",w*h(" << width << "*" << height
		   << "),w_align=" << w_align
		   << ",h_align=" << h_align
		   << ",size=" << size
		   << ",compress=" << compress
		   << "]";
		return ss.str();
	}
	uvm_decoder_para()
	{
	}
	uvm_decoder_para(uint32_t slot) : slot_id{slot}
	{
	}
};

#define UVM_IOC_MAGIC 'U'
#define UVM_IOC_SET_USAGE _IOWR(UVM_IOC_MAGIC, 9, \
				struct uvm_usage_data)

#define UVM_IOC_GET_USAGE _IOWR(UVM_IOC_MAGIC, 10, \
				struct uvm_usage_data)
#define UVM_IOC_SET_DECODER_PARA _IOWR(UVM_IOC_MAGIC, 12, \
				struct uvm_decoder_para)
#define UVM_IOC_GET_DECODER_PARA _IOWR(UVM_IOC_MAGIC, 13, \
				struct uvm_decoder_para)

/*for userspace to call uvm ioctl to add buf usage*/
bool am_gralloc_get_uvm_buf_usage(const native_handle_t * hnd, int *usage);
bool am_gralloc_set_uvm_buf_usage(const native_handle_t * hnd, int usage);
int am_gralloc_set_uvm_decoder_para(uvm_decoder_para *uvm_para);
int am_gralloc_get_uvm_decoder_para(uvm_decoder_para *uvm_para);

#endif
