/*
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 * Description:
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "am_gralloc_uvm_ext.h"
#include <sys/ioctl.h>

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

bool am_gralloc_get_uvm_buf_usage(const native_handle_t * hnd, int *usage) {
	int ret = 0;
	int uvm_fd = -1;
	private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
	if (!buffer)
        return -1;

	uvm_fd = uvm_ext_device::get_uvm();
	if (uvm_fd < 0) {
		ALOGE("%s, get_uvm failed uvm_fd=%d",
				__func__, uvm_fd);
		return ret;
	}
	struct uvm_usage_data uud = {
		.fd = buffer->share_fd,
	};
	ret = ioctl(uvm_fd, UVM_IOC_GET_USAGE, &uud);
	if (ret < 0) {
		ALOGE("%s, failed uvm_fd=%d",
			__func__, uvm_fd);
		return ret;
	}

	*usage = uud.usage;

	return ret;
}

bool am_gralloc_set_uvm_buf_usage(const native_handle_t * hnd, int usage) {
	int ret = 0;
	int uvm_fd = -1;
	private_handle_t * buffer = hnd ? private_handle_t::downcast(hnd) : NULL;
	if (!buffer)
        return -1;

	uvm_fd = uvm_ext_device::get_uvm();
	if (uvm_fd < 0) {
		ALOGE("%s, get_uvm failed uvm_fd=%d",
				__func__, uvm_fd);
		return ret;
	}
	struct uvm_usage_data uud = {
		.fd = buffer->share_fd,
		.usage = usage
	};
	ret = ioctl(uvm_fd, UVM_IOC_SET_USAGE, &uud);
	if (ret < 0) {
		ALOGE("%s, failed uvm_fd=%d",
			__func__, uvm_fd);
		return ret;
	}

	return ret;
}
