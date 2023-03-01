# Copyright (C) 2020-2022 Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Configuration that should be included by BoardConfig.mk to configure necessary Soong namespaces.

#
# Software behaviour defines
#

# The following defines are used to override default behaviour of which heap is selected for allocations.
# The default is to pick system heap.
# When enabled, uses DMA heap for when the usage has GRALLOC_USAGE_HW_FB or
# GRALLOC_USAGE_HW_COMPOSER set and GRALLOC_USAGE_HW_VIDEO_ENCODER is not set.
GRALLOC_USE_ION_DMA_HEAP?=1
ifeq ($(TARGET_APP_LAYER_USE_CONTINUOUS_BUFFER),true)
AML_ALLOC_SCANOUT_FOR_COMPOSE :=1
endif
# When enabled, allocations for display buffers will use physically contiguous memory.
GRALLOC_USE_CONTIGUOUS_DISPLAY_MEMORY?=0

# When enabled, forces format to BGRA_8888 for FB usage when HWC is in use
GRALLOC_HWC_FORCE_BGRA_8888?=0
# When enabled, disables AFBC for FB usage when HWC is in use
GRALLOC_HWC_FB_DISABLE_AFBC?=0

# When enabled, buffers will never be allocated with AFBC
GRALLOC_ARM_NO_EXTERNAL_AFBC?=0

#meson config
GRALLOC_AML_EXTEND?=1

ifeq ($(GRALLOC_AML_EXTEND),1)
BOARD_RESOLUTION_RATIO ?= 1080
endif

AML_ALLOC_SCANOUT_FOR_COMPOSE ?=0
ifeq ($(TARGET_APP_LAYER_USE_CONTINUOUS_BUFFER),true)
AML_ALLOC_SCANOUT_FOR_COMPOSE :=1
endif

ifeq ($(GRALLOC_AML_EXTEND),1)
BOARD_VALID_RESOLUTION_RATIO:= 720 1080 2160
ifeq ($(filter $(BOARD_RESOLUTION_RATIO),$(BOARD_VALID_RESOLUTION_RATIO)),)
    $(error resolution version $(BOARD_RESOLUTION_RATIO) is not valid. Valid versions are $(BOARD_VALID_RESOLUTION_RATIO))
endif
endif

# Setup configuration in Soong namespace
$(call soong_config_set,arm_gralloc,gralloc_use_ion_dma_heap,$(GRALLOC_USE_ION_DMA_HEAP))
$(call soong_config_set,arm_gralloc,gralloc_use_contiguous_display_memory,$(AML_ALLOC_SCANOUT_FOR_COMPOSE))
$(call soong_config_set,arm_gralloc,gralloc_hwc_force_bgra_8888,$(GRALLOC_HWC_FORCE_BGRA_8888))
$(call soong_config_set,arm_gralloc,gralloc_hwc_fb_disable_afbc,$(GRALLOC_HWC_FB_DISABLE_AFBC))
$(call soong_config_set,arm_gralloc,gralloc_arm_no_external_afbc,$(GRALLOC_ARM_NO_EXTERNAL_AFBC))
$(call soong_config_set,arm_gralloc,gralloc_target_product,$(TARGET_PRODUCT))
$(call soong_config_set,arm_gralloc,board_resolution,v$(BOARD_RESOLUTION_RATIO))

# Retrieve the directory of Gralloc module
LOCAL_MODULE_MAKEFILE := $(lastword $(MAKEFILE_LIST)))
GRALLOC_TOP_DIR := $(strip $(patsubst %/,%,$(dir $(LOCAL_MODULE_MAKEFILE))))

# Add the system properties for Gralloc
TARGET_VENDOR_PROP += $(GRALLOC_TOP_DIR)/arm.gralloc.usage.prop
TARGET_VENDOR_PROP += $(GRALLOC_TOP_DIR)/arm.egl.config.prop

# enable ion-heap for kernel5.4 or 4.9
ifneq ($(filter $(TARGET_BUILD_KERNEL_VERSION),5.4 4.9),)
ifeq ($(TARGET_BUILD_KERNEL_VERSION), 4.9)
BUILD_KERNEL_4_9 ?= true
$(call soong_config_set,arm_gralloc,build_kernel_4_9,$(BUILD_KERNEL_4_9))
endif
$(warning Enabled ion-heap for kernel $(TARGET_BUILD_KERNEL_VERSION))
ifneq ($(wildcard $(GRALLOC_TOP_DIR)/src/allocator/ion/Android.bp.disabled),)
RESULT := $(shell mv $(GRALLOC_TOP_DIR)/src/allocator/ion/Android.bp.disabled $(GRALLOC_TOP_DIR)/src/allocator/ion/Android.bp)
endif

ifneq ($(wildcard $(GRALLOC_TOP_DIR)/src/allocator/dma_buf_heaps/Android.bp),)
RESULT := $(shell mv $(GRALLOC_TOP_DIR)/src/allocator/dma_buf_heaps/Android.bp $(GRALLOC_TOP_DIR)/src/allocator/dma_buf_heaps/Android.bp.disabled.5.4)
endif
endif
