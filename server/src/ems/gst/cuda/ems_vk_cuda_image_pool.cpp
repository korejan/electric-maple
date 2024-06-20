// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Source - Fixed sized pool of vk_cuda_images
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup aux_util
 */
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <vulkan/vulkan_core.h>

#include "ems_vk_cuda_image_pool.h"
#include "ems_vk_cuda_image.h"

#pragma GCC diagnostic ignored "-Wnonnull"

struct vk_pooled_cuda_image final {
    vk_cuda_image base;
    bool used{false};
};

struct ems_vk_cuda_image_pool final {
    mutable std::mutex pool_mutex{};

    std::vector<vk_pooled_cuda_image> images{};
    ems_vk_cuda_image_pool_info pool_info{};

    struct vk_bundle *vk = nullptr;   

    void clear() {
        std::scoped_lock<std::mutex> lock(pool_mutex);
        for (auto& pooled_image : images) {
            if (pooled_image.base.base.handle != VK_NULL_HANDLE)
                vk->vkDestroyImage(vk->device, pooled_image.base.base.handle, nullptr);
            if (pooled_image.base.base.memory != VK_NULL_HANDLE)
                vkFreeMemory(vk->device, pooled_image.base.base.memory, nullptr);
        }
        images.clear();
    }
    ~ems_vk_cuda_image_pool() { clear(); }
};

struct ems_vk_cuda_image_pool*
ems_vk_cuda_image_pool_create(struct vk_bundle *vk,
                              const struct ems_vk_cuda_image_pool_info* create_info) {
    if (vk == nullptr || create_info == nullptr)
        return nullptr;

    auto new_pool = std::make_unique<ems_vk_cuda_image_pool>();
    new_pool->vk = vk;

    ems_create_cuda_image_info info = {
        .vk = vk,
        .alloc_callbacks = nullptr,
        .size = create_info->extent,
        .format = create_info->vk_format,
        .flags = 0,
        .image_tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
    new_pool->images.resize((std::size_t)create_info->pool_size);
    for (auto& pooled_image : new_pool->images) {
        if (ems_create_cuda_vk_image(&info, &pooled_image.base) != VK_SUCCESS) {
            return nullptr;
        }
    }
    new_pool->pool_info = *create_info;
    return new_pool.release();
}

void
ems_vk_cuda_image_pool_destroy(struct ems_vk_cuda_image_pool* pool) {
    delete pool;
}

struct vk_cuda_image*
ems_vk_cuda_image_pool_new_image(struct ems_vk_cuda_image_pool* pool) {
    if (pool == nullptr)
        return nullptr;
    auto& pool_mutex = pool->pool_mutex;
    std::scoped_lock<std::mutex> lock{pool_mutex};
    for (auto& pooled_image : pool->images) {
        if (!pooled_image.used) {
            pooled_image.used = true;
            return &pooled_image.base;
        }
    }
    return nullptr;
}

void
ems_vk_cuda_image_pool_release_image(struct ems_vk_cuda_image_pool* pool,
                                     struct vk_cuda_image* image) {
    if (pool == nullptr)
        return;
    auto& pool_mutex = pool->pool_mutex;
    std::scoped_lock<std::mutex> lock{pool_mutex};
    for (auto& pooled_image : pool->images) {
        if (&pooled_image.base == image) {
            pooled_image.used = false;
            return;
        }
    }
}

void
ems_vk_cuda_image_pool_get_info(const struct ems_vk_cuda_image_pool* pool,
                                struct ems_vk_cuda_image_pool_info* info) {
    if (pool && info) {
        *info = pool->pool_info;
    }
}
