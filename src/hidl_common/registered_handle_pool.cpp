/*
 * Copyright (C) 2020 ARM Limited. All rights reserved.
 *
 * Copyright 2016 The Android Open Source Project
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

#include "registered_handle_pool.h"

bool RegisteredHandlePool::add(buffer_handle_t bufferHandle)
{
    std::lock_guard<std::mutex> lock(mutex);
    return bufPool.insert(bufferHandle).second;
}

native_handle_t* RegisteredHandlePool::remove(void* buffer)
{
    auto bufferHandle = static_cast<native_handle_t*>(buffer);

    std::lock_guard<std::mutex> lock(mutex);
    return bufPool.erase(bufferHandle) == 1 ? bufferHandle : nullptr;
}

void RegisteredHandlePool::aml_for_each(std::function<void(const buffer_handle_t &)> fn)
{
    std::lock_guard<std::mutex> lock(mutex);
    std::for_each(bufPool.begin(), bufPool.end(), fn);
}

static bool aml_getINodeFromFd(int32_t fd, uint64_t *ino)
{
    struct stat st;
    int ret = fstat(fd, &st);

    if (ret == -1) {
        return false;
    } else {
        *ino = st.st_ino;
        MALI_GRALLOC_LOGV("%s st.st_ino:%llu st.st_size:%llu",
                __FUNCTION__, st.st_ino, st.st_size);
        return true;
    }
}

static bool aml_compare_fd(int32_t inFd, int32_t poolFd)
{
    uint64_t inFd_ino = 0;
    uint64_t poolFd_ino = 1;

    if (!aml_getINodeFromFd(inFd, &inFd_ino) ||
        !aml_getINodeFromFd(poolFd, &poolFd_ino)) {
        return false;
    } else {
        if (inFd_ino == poolFd_ino) {
            MALI_GRALLOC_LOGV("%s match fd! inFd:%d poolFd:%d inFd_ino:%llu poolFd_ino:%llu",
                __FUNCTION__, inFd, poolFd, inFd_ino, poolFd_ino);
            return true;
        }
    }
    return false;
}

buffer_handle_t RegisteredHandlePool::aml_get(const void* buffer)
{
    buffer_handle_t buf_handle = nullptr;
    const private_handle_t *InHandle = static_cast<const private_handle_t *>(buffer);

    aml_for_each([&buf_handle, InHandle](buffer_handle_t buffer) {
        const private_handle_t *poolHandle = static_cast<const private_handle_t *>(buffer);
        if (aml_compare_fd(InHandle->share_fd, poolHandle->share_fd)) {
            buf_handle = buffer;
        }
    });
    return buf_handle;
}

buffer_handle_t RegisteredHandlePool::get(const void* buffer)
{
    auto bufferHandle = static_cast<buffer_handle_t>(buffer);

    std::lock_guard<std::mutex> lock(mutex);
    return bufPool.count(bufferHandle) == 1 ? bufferHandle : nullptr;
}

void RegisteredHandlePool::for_each(std::function<void(const buffer_handle_t &)> fn)
{
    std::lock_guard<std::mutex> lock(mutex);
    std::for_each(bufPool.begin(), bufPool.end(), fn);
}