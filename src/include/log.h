/*
 * Copyright (C) 2019, 2022-2023 ARM Limited. All rights reserved.
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

#pragma once

#ifndef LOG_TAG
#define LOG_TAG "mali_gralloc"
#endif
#include <cstring>
#include <log/log.h>
#include <cutils/properties.h>

typedef enum {
	GRALLOC_LOG_LEVEL_DEBUG,
	GRALLOC_LOG_LEVEL_INFO,
	GRALLOC_LOG_LEVEL_ERR,
} GRALLOC_LOG_LEVEL;
extern GRALLOC_LOG_LEVEL gralloc_log_level;
//#define AML_GRALLOC_DEBUG 1
/* Delegate logging to Android */

#define AML_GRALLOC_LOGD(...) do { if (gralloc_log_level <= GRALLOC_LOG_LEVEL_DEBUG) ALOGD(__VA_ARGS__); } while(0)
#define AML_GRALLOC_LOGI(...) do { if (gralloc_log_level <= GRALLOC_LOG_LEVEL_INFO) ALOGI(__VA_ARGS__); } while(0)
#define AML_GRALLOC_LOGE(...) do { if (gralloc_log_level <= GRALLOC_LOG_LEVEL_ERR) ALOGE(__VA_ARGS__); } while(0)

#define MALI_GRALLOC_LOGV(...) AML_GRALLOC_LOGD(__VA_ARGS__)
#define MALI_GRALLOC_LOGI(...) AML_GRALLOC_LOGI(__VA_ARGS__)
#define MALI_GRALLOC_LOGW(...) ALOGW(__VA_ARGS__)
#define MALI_GRALLOC_LOGE(...) ALOGE(__VA_ARGS__)

static inline void get_debug_log_level()
{
	char prop[PROPERTY_VALUE_MAX];
	if (property_get("vendor.gralloc.debug.level", prop, NULL) > 0)
	{
		if (strncmp(prop, "debug", 5) == 0)
			gralloc_log_level = GRALLOC_LOG_LEVEL_DEBUG;
		else if (strncmp(prop, "info", 4) == 0)
			gralloc_log_level = GRALLOC_LOG_LEVEL_INFO;
		else if (strncmp(prop, "err", 3) == 0)
			gralloc_log_level = GRALLOC_LOG_LEVEL_ERR;

		ALOGD("gralloc debug log level change to %d(%s)", gralloc_log_level, prop);
	}
}

#ifdef __cplusplus
#include <android-base/logging.h>

/* Note that when using cpp style logging as defined below, VERBOSE logging
 * will be ignored by default on many setups. This is different to how
 * the above C style logging behaves. i.e. MALI_GRALLOC_LOGV("foo") WILL appear
 * in logcat, whereas MALI_GRALLOC_LOG(VERBOSE) << "bar" WILL NOT.
 */
#define MALI_GRALLOC_LOG(level) LOG(level)
#endif
