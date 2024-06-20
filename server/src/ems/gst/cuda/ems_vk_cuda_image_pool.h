// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Header - Fixed sized pool of vk_cuda_images
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup aux_util
 */
#pragma once

#include <vulkan/vulkan_core.h>
#include "vk/vk_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ems_vk_cuda_image_pool;

struct ems_vk_cuda_image_pool_info {
    VkExtent2D extent;
    VkFormat vk_format;
    uint32_t pool_size;
};

struct ems_vk_cuda_image_pool*
ems_vk_cuda_image_pool_create(struct vk_bundle *vk,
                              const struct ems_vk_cuda_image_pool_info* create_info);

void
ems_vk_cuda_image_pool_destroy(struct ems_vk_cuda_image_pool* pool);

struct vk_cuda_image*
ems_vk_cuda_image_pool_new_image(struct ems_vk_cuda_image_pool* pool);

void
ems_vk_cuda_image_pool_release_image(struct ems_vk_cuda_image_pool* pool,
                                     struct vk_cuda_image*);

void
ems_vk_cuda_image_pool_get_info(const struct ems_vk_cuda_image_pool* pool,
                                struct ems_vk_cuda_image_pool_info* info);

#ifdef __cplusplus
}
#endif
