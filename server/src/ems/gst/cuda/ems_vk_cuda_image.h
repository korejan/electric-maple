// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief Vulkan-CUDA image interop utils
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup aux_util
 */
#pragma once
#include <stdint.h>
#include <vulkan/vulkan_core.h>
#include <cuda_runtime_api.h>

#include "vk/vk_image_allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_cuda_image {
    struct vk_image base;
    struct {
        cudaArray_t array;
        cudaExternalMemory_t external_memory;
    } cuda;
};

struct ems_create_cuda_image_info {
    struct vk_bundle *vk;
    VkAllocationCallbacks* alloc_callbacks;
    VkExtent2D size;
    VkFormat format;
    VkImageCreateFlags flags;
    VkImageTiling image_tiling;
    VkImageUsageFlags usage;
    VkMemoryPropertyFlags memory_property_flags;
};

VkResult
ems_create_cuda_vk_image(const struct ems_create_cuda_image_info *create_info,
                         struct vk_cuda_image *out_image);

struct ems_vk_cuda_device {
    int device_id;
    uint32_t node_mask;
};

bool
ems_find_matching_cuda_device(const struct xrt_uuid *vk_device_uuid, struct ems_vk_cuda_device *vk_cuda_device);

#ifdef __cplusplus
}
#endif
