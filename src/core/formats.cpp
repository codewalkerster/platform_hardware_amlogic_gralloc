/*
 * Copyright (C) 2016-2020, 2022-2023 ARM Limited. All rights reserved.
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

#include <string.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <log/log.h>
#include <assert.h>
#include <optional>

#include <hardware/gralloc1.h>

#include "usages.h"
#include "helper_functions.h"
#include "buffer_allocation.h"
#include "format_info.h"
#include "format_selection.h"
#include "capabilities/capabilities.h"
#include "am_gralloc_internal.h"

/*
 * Determines all IP consumers included by the requested buffer usage.
 * Private usage flags are excluded from this process.
 *
 * @param usage   [in]    Buffer usage.
 *
 * @return flags word of all enabled consumers;
 *         0, if no consumers are enabled
 */
static consumers_t get_consumers(uint64_t usage)
{
	consumers_t consumers;

	/* Private usage is not applicable to consumer derivation */
	usage &= ~GRALLOC_USAGE_PRIVATE_MASK;
	/* Exclude usages also not applicable to consumer derivation */
	usage &= ~GRALLOC_USAGE_PROTECTED;

	if (usage == GRALLOC_USAGE_HW_COMPOSER)
	{
		consumers = MALI_GRALLOC_IP_DPU;
	}
	else
	{
		if (usage & GRALLOC_USAGE_SW_READ_MASK)
		{
			consumers.add(MALI_GRALLOC_IP_CPU);
		}

		/* GRALLOC_USAGE_HW_FB describes a framebuffer which contains a
		 * pre-composited scene that is scanned-out to a display. This buffer
		 * can be consumed by even the most basic display processor which does
		 * not support multi-layer composition.
		 */
		if (usage & GRALLOC_USAGE_HW_FB)
		{
			consumers.add(MALI_GRALLOC_IP_DPU);
		}

		if (usage & GRALLOC_USAGE_HW_VIDEO_ENCODER)
		{
			consumers.add(MALI_GRALLOC_IP_VPU);
		}

		/* GRALLOC_USAGE_HW_COMPOSER does not explicitly define whether the
		 * display processor is producer or consumer. When used in combination
		 * with GRALLOC_USAGE_HW_TEXTURE, it is assumed to be consumer since the
		 * GPU and DPU both act as compositors.
		 */
		if ((usage & (GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_COMPOSER)) ==
		    (GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_COMPOSER))
		{
			consumers.add(MALI_GRALLOC_IP_DPU);
		}

		if (usage & (GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_GPU_DATA_BUFFER))
		{
			consumers.add(MALI_GRALLOC_IP_GPU);
		}
		#ifdef GRALLOC_AML_EXTEND
		if (consumers.empty() && (usage & (GRALLOC_USAGE_HW_COMPOSER)))
		{
			consumers.add(MALI_GRALLOC_IP_DPU);
		}
		#endif
	}

	return consumers;
}

/*
 * Determines all IP producers included by the requested buffer usage.
 * Private usage flags are excluded from this process.
 *
 * @param usage   [in]    Buffer usage.
 *
 * @return flags word of all enabled producers;
 *         0, if no producers are enabled
 */
static producers_t get_producers(uint64_t usage)
{
	producers_t producers;

	/* Private usage is not applicable to producer derivation */
	usage &= ~GRALLOC_USAGE_PRIVATE_MASK;
	/* Exclude usages also not applicable to producer derivation */
	usage &= ~GRALLOC_USAGE_PROTECTED;

	if (usage == GRALLOC_USAGE_HW_COMPOSER)
	{
#ifdef GRALLOC_AML_EXTEND
		producers = MALI_GRALLOC_IP_DPU;
#else
		producers = MALI_GRALLOC_IP_DPU_AEU;
#endif
	}
	else
	{
		if (usage & GRALLOC_USAGE_SW_WRITE_MASK)
		{
			producers.add(MALI_GRALLOC_IP_CPU);
		}

		/* DPU is normally consumer however, when there is an alternative
		 * consumer (VPU) and no other producer (e.g. VPU), it acts as a producer.
		 */
		if ((usage & GRALLOC_USAGE_DECODER) != GRALLOC_USAGE_DECODER &&
		    (usage & (GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_VIDEO_ENCODER)) ==
		        (GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_VIDEO_ENCODER))
		{
			producers.add(MALI_GRALLOC_IP_DPU);
		}

		if (usage & (GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_GPU_DATA_BUFFER))
		{
			producers.add(MALI_GRALLOC_IP_GPU);
		}

		if (usage & GRALLOC_USAGE_HW_CAMERA_WRITE)
		{
			producers.add(MALI_GRALLOC_IP_CAM);
		}

		/* Video decoder producer is signalled by a combination of usage flags
		 * (see definition of GRALLOC_USAGE_DECODER).
		 */
		if ((usage & GRALLOC_USAGE_DECODER) == GRALLOC_USAGE_DECODER)
		{
			producers.add(MALI_GRALLOC_IP_VPU);
		}
	}

	return producers;
}

/*
 * Update buffer dimensions for producer/consumer constraints. This process is
 * not valid with CPU producer/consumer since the new resolution cannot be
 * communicated to generic clients through the public APIs. Adjustments are
 * likely to be related to AFBC.
 *
 * @param alloc_format   [in]    Format (inc. modifiers) to be allocated.
 * @param usage          [in]    Buffer usage.
 * @param width          [inout] Buffer width (in pixels).
 * @param height         [inout] Buffer height (in pixels).
 *
 * @return none.
 */
void mali_gralloc_adjust_dimensions(const internal_format_t alloc_format, const uint64_t usage, int *const width,
	int *const height)
{
	/* Determine producers and consumers. */
	const auto producers = get_producers(usage);

	/*
	 * Video producer requires additional height padding of AFBC buffers (whole
	 * rows of 16x16 superblocks). Cropping will be applied to internal
	 * dimensions to fit the public size.
	 */
	if (producers.contains(MALI_GRALLOC_IP_VPU) && alloc_format.is_afbc())
	{
		const format_info_t *info = alloc_format.get_base_info();
		if (info != nullptr)
		{
			/* 8-bit/10-bit YUV420 formats. */
			if (info->is_yuv && info->hsub == 2 && info->vsub == 2)
			{
				*height += alloc_format.get_afbc_tiled_headers() ? 16 : 32;
			}
		}
	}

	if (producers.contains(MALI_GRALLOC_IP_GPU))
	{
		/* Pad all AFBC allocations to multiple of GPU tile size. */
		if (alloc_format.is_afbc())
		{
			*width = GRALLOC_ALIGN(*width, 16);
			*height = GRALLOC_ALIGN(*height, 16);
		}
	}

#ifdef GRALLOC_AML_EXTEND
	if (need_do_width_height_align(usage, *width, *height))
	{
		*width = GRALLOC_ALIGN(*width, 64);
		if (!(usage & GRALLOC1_CONSUMER_USAGE_PRIVATE_3))
			*height = GRALLOC_ALIGN(*height, 64);
	}
#endif

	AML_GRALLOC_LOGI("%s: alloc_format(FMT:0x%x MOD:0x%x) usage=0x%" PRIx64 " alloc_width=%d alloc_height=%d",
		__FUNCTION__, alloc_format.get_format(), alloc_format.get_modifiers(), usage, *width, *height);
}

/*
 * Obtain level of support for base format across all producers and consumers as
 * defined by IP support table. This support is defined for the most capable IP -
 * specific IP might have reduced support based on specific capabilities.
 *
 * @param producers      [in]    Producers (flags).
 * @param consumers      [in]    Consumers (flags).
 * @param format         [in]    Format entry in IP support table.
 *
 * @return format support flags.
 */
static format_support_flags ip_supports_base_format(const producers_t producers, const consumers_t consumers,
	const format_ip_support_t &format)
{
	format_support_flags support = ~0;

	/* Determine producer support for base format. */
	if (producers.contains(MALI_GRALLOC_IP_CPU))
	{
		support &= format.cpu_wr;
	}
	if (producers.contains(MALI_GRALLOC_IP_GPU))
	{
		support &= format.gpu_wr;
	}
	if (producers.contains(MALI_GRALLOC_IP_DPU))
	{
		support &= format.dpu_wr;
	}
	if (producers.contains(MALI_GRALLOC_IP_DPU_AEU))
	{
		support &= format.dpu_aeu_wr;
	}
	if (producers.contains(MALI_GRALLOC_IP_CAM))
	{
		support &= format.cam_wr;
	}
	if (producers.contains(MALI_GRALLOC_IP_VPU))
	{
		support &= format.vpu_wr;
	}

	/* Determine producer support for base format. */
	if (consumers.contains(MALI_GRALLOC_IP_CPU))
	{
		support &= format.cpu_rd;
	}
	if (consumers.contains(MALI_GRALLOC_IP_GPU))
	{
		support &= format.gpu_rd;
	}
	if (consumers.contains(MALI_GRALLOC_IP_DPU))
	{
		support &= format.dpu_rd;
	}
	if (consumers.contains(MALI_GRALLOC_IP_VPU))
	{
		support &= format.vpu_rd;
	}

	return support;
}

/*
 * Obtain level of support for base format depending of the requested
 * usages passed.
 *
 * @param usages       [in]    Requested usages.
 * @param format_flags [in]    Current format support based on IP support (flags)
 *
 * @return format support flags.
 */
static format_support_flags usage_supports_base_format(uint64_t usages, format_support_flags format_flags)
{
	/* Disable all types of default compression for data buffers */
	if (usages & GRALLOC_USAGE_GPU_DATA_BUFFER)
	{
		format_flags &= F_LIN;
	}

	return format_flags;
}

/*
 * Determines whether a format is subsampled YUV, where each
 * chroma channel has fewer samples than the luma channel. The
 * sub-sampling is always a power of 2.
 *
 * @param format   [in]    Format (internal).
 *
 * @return 1, where format is subsampled YUV;
 *         0, otherwise
 */
static bool is_subsampled_yuv(const internal_format_t format)
{
	const auto *info = format.get_base_info();
	return info != nullptr && info->is_yuv && (info->hsub > 1 || info->vsub > 1);
}

/*
 * Determines whether multi-plane AFBC (requires specific IP capabilities) is
 * supported across all producers and consumers.
 *
 * @param producers      [in]    Producers (flags).
 * @param consumers      [in]    Consumers (flags).
 *
 * @return 1, multiplane AFBC is supported
 *         0, otherwise
 */
static inline bool is_afbc_multiplane_supported(const producers_t producers, const consumers_t consumers)
{
	return ip_t::support(producers, consumers, feature_t::AFBC_16X16) &&
	       ip_t::support(producers, consumers, feature_t::AFBC_TILED_HEADERS) &&
	       ip_t::support(producers, consumers, feature_t::AFBC_64X4) && producers.empty();
}

/*
 * Determines whether the format is 16 bits wide to
 * verify if it can be processed on the device.
 *
 * @param fmt_info       [in]    the format attributes .
 *
 * @return true, The format is 16 bits wide (at least one component is 16 bits)
 *         false, otherwise
 */
static bool is_format_afbc_16_bits(const format_info_t &fmt_info)
{
	for (int i = 0; i < fmt_info.npln; i++)
	{
		if (fmt_info.bpp_afbc[i] / fmt_info.ncmp[i] >= 16)
		{
			return true;
		}
	}
	return false;
}

/*
 * Determines whether a given base format is supported by all producers and
 * consumers. After checking broad support across producer/consumer IP, this
 * function uses capabilities to disable features (base formats and AFBC
 * modifiers) that are not supported by specific versions of each IP.
 *
 * @param fmt_info       [in]    Base format information.
 * @param fmt_ip_support [in]    Capabilities information for format.
 * @param usage          [in]    Buffer usage.
 * @param producers      [in]    Producers (flags).
 * @param consumers      [in]    Consumers (flags).
 *
 * @return format support flags.
 */
static format_support_flags is_format_supported(const format_info_t &fmt_info,
	const format_ip_support_t &fmt_ip_support, const uint64_t usage,
	const producers_t producers, const consumers_t consumers)
{
	/* Determine format support from table. */
	format_support_flags f_flags = ip_supports_base_format(producers, consumers, fmt_ip_support);
	/* Determine if requested usages support the format. */
	f_flags = usage_supports_base_format(usage, f_flags);

	/* Determine whether producers/consumers support required AFBC features. */
	if (f_flags & F_AFBC)
	{
		if (!fmt_info.afbc || !ip_t::support(producers, consumers, feature_t::AFBC_16X16))
		{
			f_flags &= ~F_AFBC;
		}

		/* Check that multi-plane format supported by producers/consumers. */
		if (fmt_info.npln > 1 && !is_afbc_multiplane_supported(producers, consumers))
		{
			f_flags &= ~F_AFBC;
		}

		/* Apply some additional restrictions from producers and consumers */
		/* Some modifiers affect base format support */
		if (fmt_info.is_yuv && !ip_t::support(producers, consumers, feature_t::AFBC_YUV))
		{
			f_flags &= ~F_AFBC;
		}

		if (gralloc_usage_is_frontbuffer(usage))
		{
			if (!ip_t::support(producers, consumers, feature_t::AFBC_DOUBLE_BODY))
			{
				f_flags &= ~F_AFBC;
			}
		}
		/* Check for old devices that don't support AFBC with latest format */
		if (is_format_afbc_16_bits(fmt_info) && !ip_t::support(producers, consumers, feature_t::AFBC_FORMAT_RGB16))
		{
			f_flags &= ~F_AFBC;
		}
	}
	if (f_flags & F_AFRC)
	{
		if (!fmt_info.afrc || (!ip_t::support(producers, consumers, feature_t::AFRC_ROT_LAYOUT) &&
		                       !ip_t::support(producers, consumers, feature_t::AFRC_SCAN_LAYOUT)))
		{
			f_flags &= ~F_AFRC;
		}
		/* This checks on AFRC using bpp_afbc field */
		if (is_format_afbc_16_bits(fmt_info) && !ip_t::support(producers, consumers, feature_t::AFRC_FORMAT_RGB16))
		{
			f_flags &= ~F_AFRC;
		}
	}
	if (f_flags & F_BL_YUV)
	{
		if (!(fmt_info.block_linear && fmt_info.is_yuv))
		{
			f_flags &= ~F_BL_YUV;
		}
		else if (fmt_info.bps == 8 && !ip_t::support(producers, consumers, feature_t::YUV_BL_8))
		{
			f_flags &= ~F_BL_YUV;
		}
		else if (fmt_info.bps == 10 && !ip_t::support(producers, consumers, feature_t::YUV_BL_10))
		{
			f_flags &= ~F_BL_YUV;
		}
	}
	if (f_flags != F_NONE)
	{
		if (fmt_info.id == MALI_GRALLOC_FORMAT_INTERNAL_RGBA_1010102 &&
		    !ip_t::support(producers, consumers, feature_t::FORMAT_R10G10B10A2))
		{
			f_flags = F_NONE;
		}
		else if (fmt_info.id == MALI_GRALLOC_FORMAT_INTERNAL_RGBA_16161616)
		{
			if (!ip_t::support(producers, consumers, feature_t::FORMAT_R16G16B16A16_FLOAT))
			{
				f_flags = F_NONE;
			}
			else if (!ip_t::support(producers, consumers, feature_t::AFBC_FORMAT_R16G16B16A16_FLOAT))
			{
				f_flags = F_LIN;
			}
		}
		else if (fmt_info.id == MALI_GRALLOC_FORMAT_INTERNAL_RGBA_10101010 &&
		         !ip_t::support(producers, consumers, feature_t::FORMAT_R10G10B10A10))
		{
			f_flags = F_NONE;
		}
	}

	return f_flags;
}

/*
 * Ensures that the allocation format conforms to the AFBC specification and is
 * supported by producers and consumers. Format modifiers are (in most cases)
 * disabled as required to make valid. It is important to first resolve invalid
 * combinations which are not dependent upon others to reduce the possibility of
 * circular dependency.
 *
 * @param alloc_format          [in]    Allocation format (base + modifiers).
 *
 * @return valid alloc_format with AFBC possibly disabled (if required)
 */
static internal_format_t validate_afbc_format(internal_format_t alloc_format, const producers_t producers,
	const consumers_t consumers)
{
	const mali_gralloc_internal_format base_format = alloc_format.get_base();

	/*
	 * AFBC with tiled-headers must be enabled for AFBC front-buffer-safe allocations.
	 * NOTE: format selection algorithm will always try and enable AFBC with
	 * tiled-headers where supported by producer(s) and consumer(s).
	 */
	if (alloc_format.get_afbc_double_body())
	{
		/* Disable (extra-) wide-block which is unsupported with front-buffer safe AFBC. */
		alloc_format.set_afbc_32x8(false);
		alloc_format.set_afbc_64x4(false);
	}

	/*
	 * AFBC specification: Split-block is not supported for
	 * subsampled formats (YUV) when wide-block is enabled.
	 */
	if (alloc_format.get_afbc_32x8() && alloc_format.get_afbc_block_split() && is_subsampled_yuv(alloc_format))
	{
		/* Disable split-block instead of wide-block because because
		 * wide-block has greater impact on display performance.
		 */
		alloc_format.set_afbc_block_split(false);
	}

	/* AFBC specification: Split-block must be enabled for
	 * non-subsampled formats > 16 bpp, where wide-block is enabled.
	 */
	if (alloc_format.get_afbc_32x8() && !alloc_format.get_afbc_block_split() && !is_subsampled_yuv(alloc_format) &&
	    base_format != MALI_GRALLOC_FORMAT_INTERNAL_RGB_565)
	{
		/* Enable split-block if supported by producer(s) & consumer(s),
		 * otherwise disable wide-block.
		 */
		if (ip_t::support(producers, consumers, feature_t::AFBC_BLOCK_SPLIT))
		{
			alloc_format.set_afbc_block_split();
		}
		else
		{
			alloc_format.set_afbc_32x8(false);
		}
	}

	/* Some RGB formats don't support split block. */
	if (base_format == MALI_GRALLOC_FORMAT_INTERNAL_RGB_565)
	{
		alloc_format.set_afbc_block_split(false);
	}

	/* Ensure that AFBC features are supported by producers/consumers. */
	if (alloc_format.is_afbc() && !ip_t::support(producers, consumers, feature_t::AFBC_16X16))
	{
		MALI_GRALLOC_LOGE("AFBC basic selected but not supported by producer/consumer. Disabling AFBC");
		alloc_format.clear_modifiers();
	}

	if (alloc_format.get_afbc_block_split() && !ip_t::support(producers, consumers, feature_t::AFBC_BLOCK_SPLIT))
	{
		MALI_GRALLOC_LOGE("AFBC split-block selected but not supported by producer/consumer. Disabling split-block");
		alloc_format.set_afbc_block_split(false);
	}

	if (alloc_format.get_afbc_32x8() && !ip_t::support(producers, consumers, feature_t::AFBC_32X8))
	{
		MALI_GRALLOC_LOGE("AFBC wide-block selected but not supported by producer/consumer. Disabling wide-block");
		alloc_format.set_afbc_32x8(false);
	}

	if (alloc_format.get_afbc_tiled_headers() && !ip_t::support(producers, consumers, feature_t::AFBC_TILED_HEADERS))
	{
		MALI_GRALLOC_LOGE("AFBC tiled-headers selected but not supported by producer/consumer. "
		                  "Disabling tiled-headers");
		alloc_format.set_afbc_tiled_headers(false);
	}

	if (!alloc_format.get_afbc_sparse() && (!producers.support(feature_t::AFBC_WRITE_NON_SPARSE) || producers.empty()))
	{
		MALI_GRALLOC_LOGE("AFBC sparse not selected while producer cannot write non-sparse. Enabling AFBC sparse");
		alloc_format.set_afbc_sparse();
	}

	return alloc_format;
}

/*
 * Calculates the data type which Gralloc uses internally per format and usage.
 *
 * @param hal_format [in]    Descriptor for base format.
 * @param usage      [in]    Buffer usage.
 *
 * @return mali_gralloc_format_data_type value.
 */
mali_gralloc_format_data_type calc_format_data_type(const uint64_t hal_format, const uint64_t usage)
{
	/* TODO: GPUCORE-37452 - AFBC Bayer format support - add mechanism for the setter */
	(void)hal_format;
	(void)usage;
	return mali_gralloc_format_data_type::UNORM;
}

/*
 * Derives a valid AFRC format (via modifiers) for all producers and consumers.
 *
 * @param format     [in]    Descriptor for base format.
 * @param usage      [in]    Buffer usage.
 * @param producers  [in]    Buffer producer capabilities (intersection).
 * @param consumers  [in]    Buffer consumer capabilities (intersection).
 *
 * @return valid AFRC format, where modifiers are enabled (supported/preferred);
 *         base format without modifiers, otherwise
 */
static internal_format_t get_afrc_format(const format_info_t &format, const uint64_t usage, const producers_t producers,
	const consumers_t consumers)
{
	auto base_format = internal_format_t::from_android(format.id, calc_format_data_type(format.id, usage));
	auto alloc_format = base_format;

	if (ip_t::support(producers, consumers, feature_t::AFRC_ROT_LAYOUT))
	{
		alloc_format.make_afrc();
		alloc_format.set_afrc_rot_layout();
	}
	else if (ip_t::support(producers, consumers, feature_t::AFRC_SCAN_LAYOUT))
	{
		alloc_format.make_afrc();
	}
	else
	{
		return base_format;
	}

	switch (usage & MALI_GRALLOC_USAGE_AFRC_RGBA_LUMA_CODING_SIZE_MASK)
	{
	case MALI_GRALLOC_USAGE_AFRC_RGBA_LUMA_CODING_SIZE_16:
		alloc_format.set_afrc_luma_coding_size(afrc_coding_unit_size_t::bytes_16);
		break;
	case MALI_GRALLOC_USAGE_AFRC_RGBA_LUMA_CODING_SIZE_24:
		alloc_format.set_afrc_luma_coding_size(afrc_coding_unit_size_t::bytes_24);
		break;
	case MALI_GRALLOC_USAGE_AFRC_RGBA_LUMA_CODING_SIZE_32:
		alloc_format.set_afrc_luma_coding_size(afrc_coding_unit_size_t::bytes_32);
		break;
	default:
		return base_format;
	}

	if (format.is_yuv)
	{
		switch (usage & MALI_GRALLOC_USAGE_AFRC_CHROMA_CODING_SIZE_MASK)
		{
		case MALI_GRALLOC_USAGE_AFRC_CHROMA_CODING_SIZE_16:
			alloc_format.set_afrc_chroma_coding_size(afrc_coding_unit_size_t::bytes_16);
			break;
		case MALI_GRALLOC_USAGE_AFRC_CHROMA_CODING_SIZE_24:
			alloc_format.set_afrc_chroma_coding_size(afrc_coding_unit_size_t::bytes_24);
			break;
		case MALI_GRALLOC_USAGE_AFRC_CHROMA_CODING_SIZE_32:
			alloc_format.set_afrc_chroma_coding_size(afrc_coding_unit_size_t::bytes_32);
			break;
		default:
			MALI_GRALLOC_LOGE("YUV AFRC format but no AFRC UV coding size specified via usage.");
			return base_format;
		}
	}
	else if (usage & MALI_GRALLOC_USAGE_AFRC_CHROMA_CODING_SIZE_MASK)
	{
		MALI_GRALLOC_LOGE("AFRC UV coding size in usage is not compatible with non-YUV format.");
		return base_format;
	}

	return alloc_format;
}

/*
 * Derives a valid AFBC format (via modifiers) for all producers and consumers.
 * Formats are validated after enabling the largest feature set supported (and
 * desirable) for the IP usage. Some format modifier combinations are not
 * compatible. See MALI_GRALLOC_INTFMT_* modifiers for more information.
 *
 * @param format     [in]    Descriptor for base format.
 * @param usage      [in]    Buffer usage.
 * @param producer   [in]    Buffer producers (write).
 * @param consumer   [in]    Buffer consumers (read).
 *
 * @return valid AFBC format, where modifiers are enabled (supported/preferred);
 *         base format without modifiers, otherwise
 */
static internal_format_t get_afbc_format(const format_info_t &format, const uint64_t usage, const producers_t producers,
	const consumers_t consumers)
{
	const auto base_format = internal_format_t::from_android(format.id, calc_format_data_type(format.id, usage));

	if (format.is_yuv)
	{
		/* Avoid AFBC if format is YUV and any of the consumers cannot read AFBC YUV. */
		if (!consumers.empty() && !consumers.support(feature_t::AFBC_YUV))
		{
			return base_format;
		}
		/* Avoid AFBC if format is YUV and producer cannot write AFBC YUV. */
		if (!producers.support(feature_t::AFBC_YUV))
		{
			return base_format;
		}
	}

	/* AFBC is not supported for video transcode (VPU --> VPU) */
	if (producers.contains(MALI_GRALLOC_IP_VPU) && consumers.contains(MALI_GRALLOC_IP_VPU))
	{
		return base_format;
	}

	/*
	 * Determine AFBC modifiers where capabilities are defined for all producers
	 * and consumers.
	 */
	if (!ip_t::support(producers, consumers, feature_t::AFBC_16X16))
	{
		return base_format;
	}

	auto alloc_format = base_format;
	alloc_format.make_afbc();
	alloc_format.set_afbc_yuv_transform(format.yuv_transform);

	if (producers.empty() || !producers.support(feature_t::AFBC_WRITE_NON_SPARSE))
	{
		alloc_format.set_afbc_sparse();
	}

	if (ip_t::support(producers, consumers, feature_t::AFBC_TILED_HEADERS))
	{
		alloc_format.set_afbc_tiled_headers();

		if (gralloc_usage_is_frontbuffer(usage) && ip_t::support(producers, consumers, feature_t::AFBC_DOUBLE_BODY))
		{
			alloc_format.set_afbc_double_body();
		}
	}

	/* Specific producer/consumer combinations (e.g. GPU --> DPU) benefit from additional AFBC
	 * features.
	 */
	if (producers.contains(MALI_GRALLOC_IP_GPU) && consumers.contains(MALI_GRALLOC_IP_DPU) &&
	    ip_t::present(MALI_GRALLOC_IP_DPU))
	{
		/* AFBC wide-block is not supported across IP for YUV formats. */
		if (ip_t::support(producers, consumers, feature_t::AFBC_32X8) && !format.is_yuv)
		{
			/* NOTE: assume that all AFBC layers are pre-rotated. 16x16 SB must be used with
			 * DPU consumer when rotation is required.
			 */
			alloc_format.set_afbc_32x8();
		}

		if (ip_t::support(producers, consumers, feature_t::AFBC_BLOCK_SPLIT))
		{
			bool enable_split_block = true;

			/* All GPUs that can write YUV AFBC can only do it in 16x16, optionally with tiled headers. */
			if (format.is_yuv && producers.contains(MALI_GRALLOC_IP_GPU))
			{
				enable_split_block = false;
			}

			/* DPU does not support split-block other than RGB(A) 24/32-bit */
			if (!format.is_rgb || format.bpp[0] < 24)
			{
				if (producers.contains(MALI_GRALLOC_IP_DPU_AEU) || consumers.contains(MALI_GRALLOC_IP_DPU))
				{
					enable_split_block = false;
				}
			}

			alloc_format.set_afbc_block_split(enable_split_block);
		}
	}

	return validate_afbc_format(alloc_format, producers, consumers);
}

static internal_format_t get_bl_format(const internal_format_t base_format, const producers_t producers,
	const consumers_t consumers)
{
	internal_format_t alloc_format = base_format;
	if (ip_t::support(producers, consumers, feature_t::YUV_BL_8) ||
	    ip_t::support(producers, consumers, feature_t::YUV_BL_10))
	{
		alloc_format.make_block_linear();
	}
	return alloc_format;
}

/**
 * @brief Information returned by get_supported_format().
 */
struct fmt_props_t
{
	format_support_flags f_flags = 0;
	internal_format_t format;
};

/**
 * @brief Obtains support flags and modifiers for base format.
 *
 * @param fmt_info       [in]    Information for the base format for which to deduce support.
 * @param usage          [in]    Buffer usage.
 * @param producers      [in]    Producers (flags).
 * @param consumers      [in]    Consumers (flags).
 *
 * @return The @c fmt_props_t structure for the supported format, or @c std::nullopt
 */
static std::optional<fmt_props_t> get_supported_format(const format_info_t &fmt_info, const uint64_t usage,
	const producers_t producers, const consumers_t consumers)
{
	const auto base_format = internal_format_t::from_android(fmt_info.id, calc_format_data_type(fmt_info.id, usage));
	const auto *fmt_ip_support = get_format_ip_support(fmt_info.id);
	if (fmt_ip_support == nullptr)
	{
		/* Return undefined base format. */
		MALI_GRALLOC_LOG(ERROR) << "Failed to find IP support info for format id: " << base_format;
		return std::nullopt;
	}

	auto fmt_flags = is_format_supported(fmt_info, *fmt_ip_support, usage, producers, consumers);

	AML_GRALLOC_LOGI("%s: Format(%u): IP support: 0x%x", __FUNCTION__, fmt_info.id, fmt_flags);

	if (fmt_flags == F_NONE && consumers.contains(MALI_GRALLOC_IP_GPU) && consumers.contains(MALI_GRALLOC_IP_DPU))
	{
		/* If the GPU and DPU are both in the list of consumers, then we assume that composition will fall back
		 * to the GPU when the DPU does not support the format. So we remove the DPU from the list of consumers.
		 */
		auto consumers_nodpu = consumers;
		consumers_nodpu.remove(MALI_GRALLOC_IP_DPU);
		fmt_flags = is_format_supported(fmt_info, *fmt_ip_support, usage, producers, consumers_nodpu);
	}

#ifdef GRALLOC_HWC_FB_DISABLE_AFBC
	if (GRALLOC_HWC_FB_DISABLE_AFBC && DISABLE_FRAMEBUFFER_HAL && (usage & GRALLOC_USAGE_HW_FB))
	{
		/* Override capabilities to disable non linear formats for DRM HWC framebuffer surfaces. */
		fmt_flags &= ~(F_AFBC | F_AFRC | F_BL_YUV);
	}
#endif

	if (fmt_flags & F_AFRC)
	{
		const auto afrc_format = get_afrc_format(fmt_info, usage, producers, consumers);
		if (afrc_format.is_afrc())
		{
			fmt_props_t ret{ F_AFRC, afrc_format };
			AML_GRALLOC_LOGI("AFRC format: FMT:0x%x, MOD:0x%x",
				ret.format.get_format(), ret.format.get_modifiers());
			return ret;
		}
	}

	if (fmt_flags & F_AFBC)
	{
		if (gralloc_usage_is_no_afbc(usage))
		{
			/* Disable AFBC when forced by usage. */
			AML_GRALLOC_LOGI("AFBC explicitly disabled via usage");
		}
		else
		{
			const auto afbc_format = get_afbc_format(fmt_info, usage, producers, consumers);
			if (afbc_format.is_afbc())
			{
				/* Check that AFBC features are correct for multiplane format. */
				auto alloc_type = get_alloc_type(afbc_format, usage);
				if (alloc_type.has_value() && (fmt_info.npln == 1 || alloc_type->is_multi_plane))
				{
					fmt_props_t ret{ F_AFBC, afbc_format };
					AML_GRALLOC_LOGI("AFBC format: FMT:0x%x, MOD:0x%x",
						ret.format.get_format(), ret.format.get_modifiers());
					return ret;
				}
			}
		}
	}

	if (fmt_flags & F_BL_YUV)
	{
		const auto bl_format = get_bl_format(base_format, producers, consumers);
		if (bl_format.is_block_linear())
		{
			fmt_props_t ret{ F_BL_YUV, bl_format };
			AML_GRALLOC_LOGI("BL format: FMT:0x%x, MOD:0x%x",
				ret.format.get_format(), ret.format.get_modifiers());
			return ret;
		}
	}

	if (fmt_flags & F_LIN)
	{
		fmt_props_t ret{ F_LIN, base_format };
		AML_GRALLOC_LOGI("LIN format: FMT:0x%x, MOD:0x%x",
				ret.format.get_format(), ret.format.get_modifiers());
		return ret;
	}

	AML_GRALLOC_LOGI("No format selected");
	return std::nullopt;
}

/*
 * Determines whether two base formats have comparable 'color' components. Alpha
 * is considered unimportant for YUV formats.
 *
 * @param f_old     [in]    Format properties (old format).
 * @param f_new     [in]    Format properties (new format).
 *
 * @return 1, format components are equivalent
 *         0, otherwise
 */
static bool comparable_components(const format_info_t &f_old, const format_info_t &f_new)
{
	if (f_old.is_yuv && f_new.bps == f_old.bps)
	{
		/* Formats have the same number of components. */
		if (f_new.total_components() == f_old.total_components())
		{
			return true;
		}

		/* Alpha component can be dropped for yuv formats.
		 * This assumption is required for mapping Y0L2 to
		 * single plane 10-bit YUV420 AFBC.
		 */
		if (f_old.has_alpha)
		{
			if (f_new.total_components() == 3 && f_new.is_yuv && !f_new.has_alpha)
			{
				return true;
			}
		}
	}
	else if (f_old.is_rgb)
	{
		if (f_new.total_components() == f_old.total_components())
		{
			if (f_new.bpp[0] == f_old.bpp[0] && f_new.bps == f_old.bps)
			{
				return true;
			}
			if ((f_old.id == MALI_GRALLOC_FORMAT_INTERNAL_RGBX_8888 && f_new.bpp[0] == 24) ||
			    (f_old.id == MALI_GRALLOC_FORMAT_INTERNAL_RGB_565 && f_new.bpp[0] == 24))
			{
				return true;
			}
		}
	}
	else
	{
		if (f_new.id == f_old.id)
		{
			return true;
		}
	}

	return false;
}

/*
 * Determines whether two base formats are compatible such that data from one
 * format could be accurately represented/interpreted in the other format.
 *
 * @param f_old     [in]    Format properties (old format).
 * @param f_new     [in]    Format properties (new format).
 *
 * @return 1, formats are equivalent
 *         0, otherwise
 */
static bool is_format_compatible(const format_info_t &f_old, const format_info_t &f_new)
{
	if (f_new.hsub == f_old.hsub && f_new.vsub == f_old.vsub && f_new.is_rgb == f_old.is_rgb &&
	    f_new.is_yuv == f_old.is_yuv && comparable_components(f_old, f_new))
	{
		return true;
	}
	else
	{
		return false;
	}
}

/**
 * @brief Provide a grade for a candidate format with respect to the requested format.
 *
 * Used to find the best compatible format to allocate.
 *
 * @param fmt[in]    A candidate format for which this function computes a "grade".
 * @param req_format Requested base format.
 *
 * @return The grade of the compatible format. Higher is better. Returns 0 if format extensions are incompatible with
 * requested format.
 */
uint64_t grade_format(const internal_format_t fmt, uint32_t req_format)
{
	uint64_t grade = 1;

	const auto *req_info = get_format_info(req_format);
	CHECK(req_info != nullptr);
	const auto &base_info = fmt.base_info();

	if (fmt.is_afrc())
	{
		if (req_format == fmt.get_base() || is_same_or_components_reordered(*req_info, base_info))
		{
			grade++;
		}
	}
	else if (req_info->is_rgb && req_info->bpp[0] != base_info.bpp[0])
	{
		return 0;
	}

	static const struct
	{
		mali_gralloc_internal_format fmt_ext;
		uint64_t value;
	} fmt_ext_values[]{
		/* clang-format off */
		{ MALI_GRALLOC_INTFMT_AFBC_BASIC, 1 << 1 },
		{ MALI_GRALLOC_INTFMT_AFBC_SPLITBLK, 1 },
		{ MALI_GRALLOC_INTFMT_AFBC_WIDEBLK, 1 },
		{ MALI_GRALLOC_INTFMT_AFBC_TILED_HEADERS, 1 },
		{ MALI_GRALLOC_INTFMT_AFBC_EXTRAWIDEBLK, 1 },
		{ MALI_GRALLOC_INTFMT_AFBC_DOUBLE_BODY, 1 },
		{ MALI_GRALLOC_INTFMT_AFBC_BCH, 1 },
		{ MALI_GRALLOC_INTFMT_AFBC_YUV_TRANSFORM, 1 },
		{ MALI_GRALLOC_INTFMT_AFBC_SPARSE, 1 },
		{ MALI_GRALLOC_INTFMT_BLOCK_LINEAR_BASIC, 1 },
		{ MALI_GRALLOC_INTFMT_AFRC_BASIC, 1 << 30 },
		/* clang-format on */
	};
	for (auto &ext : fmt_ext_values)
	{
		if (fmt.get_modifiers() & ext.fmt_ext)
		{
			grade += ext.value;
		}
	}

	return grade;
}

/*
 * Obtains the 'best' allocation format for requested format and usage:
 * 1. Find compatible base formats (based on format properties alone)
 * 2. Find base formats supported by producers/consumers
 * 3. Find best modifiers from supported base formats
 * 4. Select allocation format from "best" base format with "best" modifiers
 *
 * NOTE: Base format re-mapping should not take place when CPU usage is
 * requested.
 *
 * @param req_base_format       [in]    Base format requested by client.
 * @param usage                 [in]    Buffer usage.
 * @param producers             [in]    Producers (flags).
 * @param consumers             [in]    Consumers (flags).
 *
 * @return alloc_format, supported for usage;
 *         MALI_GRALLOC_FORMAT_INTERNAL_UNDEFINED, otherwise
 */
static internal_format_t get_best_format(const uint32_t req_base_format, const uint64_t usage,
	const producers_t producers, const consumers_t consumers)
{
	MALI_GRALLOC_LOGV("req_base_format: 0x%" PRIx32, req_base_format);
	CHECK_NE(req_base_format, MALI_GRALLOC_FORMAT_INTERNAL_UNDEFINED);

	const auto *req_fmt_info = get_format_info(req_base_format);
	CHECK(req_fmt_info != nullptr);

	/* Find base formats supported by IP and among them, find the highest
	 * number of modifier enabled format and check if requested format is present
	 */

	int32_t num_supported_formats = 0;
	uint64_t req_format_grade = 0;
	uint64_t best_fmt_grade = 0;
	internal_format_t first_of_best_formats;
	internal_format_t req_format;

	for (const auto &fmt_info : get_all_base_formats())
	{
		if (!is_format_compatible(*req_fmt_info, fmt_info))
		{
			continue;
		}

		MALI_GRALLOC_LOGV("Compatible: Base-format: 0x%" PRIx32, fmt_info.id);

		if (auto fmt = get_supported_format(fmt_info, usage, producers, consumers))
		{
			const uint64_t sup_fmt_grade = grade_format(fmt->format, req_base_format);
			if (sup_fmt_grade)
			{
				num_supported_formats++;
				AML_GRALLOC_LOGI("Supported: Format(FMT:0x%x MOD:0x%x), Flags: 0x%x",
					fmt->format.get_format(), fmt->format.get_modifiers(), fmt->f_flags);

				/* 3. Find best modifiers from supported base formats */
				if (sup_fmt_grade > best_fmt_grade)
				{
					best_fmt_grade = sup_fmt_grade;
					first_of_best_formats = fmt->format;
				}

				/* Check if current supported format is same as requested format */
				if (fmt->format.get_base() == req_base_format)
				{
					req_format_grade = sup_fmt_grade;
					req_format = fmt->format;
				}
			}
		}
	}

	/* 4. Select allocation format from "best" base format with "best" modifiers */
	internal_format_t alloc_format;
	if (num_supported_formats > 0)
	{
		/* Select first/one of best format when requested format is either not
		 * supported or requested format is not the best format.
		 */
		if ((req_format_grade != best_fmt_grade) &&
		    (!producers.contains(MALI_GRALLOC_IP_CPU) && !consumers.contains(MALI_GRALLOC_IP_CPU)))
		{
			alloc_format = first_of_best_formats;
		}
		else if (req_format_grade != 0)
		{
			alloc_format = req_format;
		}
	}

	AML_GRALLOC_LOGI("Selected format: FMT:0x%x, MOD:0x%x", alloc_format.get_format(), alloc_format.get_modifiers());
	return alloc_format;
}

static bool is_format_multiplane_afbc(internal_format_t format)
{
	return format.is_afbc() && format.get_afbc_64x4() && format.get_afbc_tiled_headers();
}

/**
 * Check whether the given format is compatible with the given base format.
 *
 * @param format_info           [in]    Information about the selected base format.
 * @param candidate_format      [in]    Format to check
 *
 * @retval true if the modifiers @p candidate_format is compatible with @p format_info
 * @retval false if @p candidate_format is not compatible
 */
static bool check_modifiers_against_format(const format_info_t &format_info, const internal_format_t candidate_format)
{
	if (candidate_format.is_afrc() && format_info.afrc)
	{
		return true;
	}
	else if (!candidate_format.has_modifiers() || (candidate_format.is_block_linear() && format_info.block_linear))
	{
		/* Linear and block linear formats have no forced fallback. */
		return true;
	}
	else if (candidate_format.is_afbc())
	{
		if (format_info.afbc && (format_info.npln == 1 || is_format_multiplane_afbc(candidate_format)))
		{
			/* Requested format modifiers are suitable for base format. */
			return true;
		}
	}

	return false;
}

/**
 * @brief Given a forced format, construct the corresponding internal gralloc representation.
 *
 * @param req_format Requested format as provided to the IMapper API. This must be a forced format, obtained
 *   using Gralloc's mali_gralloc_format_wrapper function.
 *
 * @return The internal representation of the format packing modifiers and format in one type.
 */
static internal_format_t select_forced_format(const mali_gralloc_android_format req_format)
{
	/* The requested format is not a regular Android format, but rather a forced format.
	 * Forced formats are specific to the Arm reference Gralloc and pack in 32-bit the base
	 * format and the modifier. Extract them!
	 */
	auto int_format = internal_format_t::from_private(req_format);
	auto req_format_base = int_format.get_base();
	auto req_format_modifiers = int_format.get_modifiers();

	/* Find the internal representation of the format. */
	uint32_t internal_format = get_internal_format(req_format_base);
	const auto *format_info = get_format_info(internal_format);
	if (internal_format == MALI_GRALLOC_FORMAT_INTERNAL_UNDEFINED || format_info == nullptr)
	{
		MALI_GRALLOC_LOGE("Invalid forced format! internal_format = 0x%" PRIx32 ", req_format = 0x%" PRIx32,
		                  internal_format, req_format);
		return internal_format_t::invalid;
	}

	/* Create a candidate format. */
	auto candidate_format = internal_format_t::from_private(internal_format | req_format_modifiers);

	/* Check that the format modifiers are supported for this format. */
	if (!check_modifiers_against_format(*format_info, candidate_format))
	{
		MALI_GRALLOC_LOGE("Invalid modifiers for req_format = 0x%" PRIx32, req_format);
		return internal_format_t::invalid;
	}

	return candidate_format;
}

/**
 * @brief Given a HAL format, select the internal Gralloc format according to usage and IP capabilities.
 *
 * @param descriptor Buffer descriptor.
 * @param usage      Usages as provided to the IMapper API.
 *
 * @return The internal representation of the format packing modifiers and format in one type.
 */
static internal_format_t select_best_format(const buffer_descriptor_t &descriptor, const uint64_t usage)
{
	const mali_gralloc_android_format req_format = descriptor.hal_format;
	const uint32_t req_base_format = get_internal_format(req_format);
	const auto *format_info = get_format_info(req_base_format);

	if (req_base_format == MALI_GRALLOC_FORMAT_INTERNAL_UNDEFINED || format_info == nullptr)
	{
		MALI_GRALLOC_LOGE("Invalid base format! req_base_format = 0x%" PRIx32 ", req_format = 0x%" PRIx32,
		                  req_base_format, req_format);
		return internal_format_t::invalid;
	}

	/* Verify the usage restrictions (format_info->permitted_usage) for this format match the value of 'usage'
	 * (ignoring the VENDOR_USAGE). */
	const auto permitted_format_usage = format_info->permitted_usage;
	if (((permitted_format_usage | usage) & ~VENDOR_USAGE) != (permitted_format_usage & ~VENDOR_USAGE))
	{
		MALI_GRALLOC_LOGE("Usage not permitted! format = %#" PRIx32 ", permitted usage bits = %#" PRIx64
		                  ",usage = %#" PRIx64 ", invalid usage bits: %#" PRIx64,
		                  format_info->id, static_cast<uint64_t>(permitted_format_usage) & ~VENDOR_USAGE,
		                  static_cast<uint64_t>(usage) & ~VENDOR_USAGE,
		                  ((~permitted_format_usage & usage) & ~VENDOR_USAGE));

		return internal_format_t::invalid;
	}

	/* Determine producers and consumers. */
	auto producers = get_producers(usage);
	const auto consumers = get_consumers(usage);

	if (producers.empty() && consumers.empty())
	{
		MALI_GRALLOC_LOGE("Producer and consumer not identified.");
		return internal_format_t::invalid;
	}
	else if (producers.empty() || consumers.empty())
	{
		MALI_GRALLOC_LOGV("Producer or consumer not identified.");
	}

	/* If no producers are identified, assume the CPU is the producer. */
	if (producers.empty())
	{
		producers.add(MALI_GRALLOC_IP_CPU);
	}

	if (gralloc_usage_is_no_afbc(usage) && format_info->is_yuv)
	{
		MALI_GRALLOC_LOGE("Invalid usage 'MALI_GRALLOC_USAGE_NO_AFBC' when allocating YUV formats");
		return internal_format_t::invalid;
	}

	if (((descriptor.flags & GPU_DATA_BUFFER_WITH_ANY_FORMAT) == 0) && (usage & GRALLOC_USAGE_GPU_DATA_BUFFER) &&
	    (req_base_format != MALI_GRALLOC_FORMAT_INTERNAL_BLOB))
	{
		MALI_GRALLOC_LOGE("Invalid usage 'GRALLOC_USAGE_GPU_DATA_BUFFER' as format is not HAL_PIXEL_FORMAT_BLOB and "
		                  "Gralloc is not using AIDL allocator");
		return internal_format_t::invalid;
	}
	if (((descriptor.flags & USE_AIDL_FRONTBUFFER_USAGE) == 0) && (usage & GRALLOC_USAGE_FRONTBUFFER))
	{
		MALI_GRALLOC_LOGE("FRONT_BUFFER usage not supported");
		return internal_format_t::invalid;
	}

	if (req_base_format == MALI_GRALLOC_FORMAT_INTERNAL_R8 && ((descriptor.flags & SUPPORTS_R8) == 0))
	{
		MALI_GRALLOC_LOGE("Requested R8 format is not supported with this allocator. R8 format is only supported with "
		                  "the AIDL allocator");
		return internal_format_t::invalid;
	}

	auto alloc_format = get_best_format(format_info->id, usage, producers, consumers);

	/* Some display controllers expect the framebuffer to be in BGRX format, hence we force the format to avoid colour swap issues. */
#if defined(GRALLOC_HWC_FORCE_BGRA_8888) && defined(DISABLE_FRAMEBUFFER_HAL)
	if (GRALLOC_HWC_FORCE_BGRA_8888 && DISABLE_FRAMEBUFFER_HAL && (usage & GRALLOC_USAGE_HW_FB))
	{
		if (alloc_format.get_base() != HAL_PIXEL_FORMAT_BGRA_8888 &&
		    usage & (GRALLOC_USAGE_SW_WRITE_MASK | GRALLOC_USAGE_SW_READ_MASK))
		{
			MALI_GRALLOC_LOGE("Format unsuitable for both framebuffer usage and CPU access. Failing allocation.");
			return internal_format_t::invalid;
		}
		alloc_format =
		    internal_format_t::from_android(HAL_PIXEL_FORMAT_BGRA_8888, mali_gralloc_format_data_type::UNORM);
	}
#endif

	return alloc_format;
}

/*
 * Select pixel format (base + modifier) for allocation.
 *
 * @param descriptor       [in]   Buffer descriptor.
 * @param usage            [in]   Buffer usage.
 *
 * @return alloc_format, format to be used in allocation;
 *         MALI_GRALLOC_FORMAT_INTERNAL_UNDEFINED, where no suitable
 *         format could be found.
 */
internal_format_t mali_gralloc_select_format(const buffer_descriptor_t &descriptor, const uint64_t usage)
{
	const mali_gralloc_android_format req_format = descriptor.hal_format;
	/* Reject if usage specified is outside allowed list of valid usages. */
	if ((usage & (~VALID_USAGE)) != 0)
	{
		MALI_GRALLOC_LOGE("Invalid usage specified: 0x%" PRIx64, usage);
		return internal_format_t::invalid;
	}

	internal_format_t alloc_format;
#if defined(GRALLOC_USE_PRIVATE_FORMATS) && GRALLOC_USE_PRIVATE_FORMATS
	if (mali_gralloc_format_is_private(req_format))
	{
		alloc_format = select_forced_format(req_format);
	}
	else
#elif !defined(GRALLOC_USE_PRIVATE_FORMATS)
#error "GRALLOC_USE_PRIVATE_FORMATS must be defined"
#endif
	{
		alloc_format = select_best_format(descriptor, usage);
	}

	AML_GRALLOC_LOGI("mali_gralloc_select_format: req_format= 0x%x, usage=0x%" PRIx64 ", FMT:0x%x, MOD:0x%x",
		req_format, usage, alloc_format.get_format(), alloc_format.get_modifiers());

	return alloc_format;
}
