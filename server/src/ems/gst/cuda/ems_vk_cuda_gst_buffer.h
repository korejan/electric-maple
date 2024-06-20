// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief header - Vulkan-CUDA - GstBuffer wrapping helper.
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup aux_util
 */
#pragma once
#include <stdint.h>
#include <gst/gst.h>
#include <gst/gstbuffer.h>
#include <gst/cuda/gstcuda.h>
#include <gst/cuda/gstcudamemory.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ems_gst_buffer_new_wrapped_cuda_info {
    GstCudaAllocator *allocator;
    GstCudaContext   *context;
    GstCudaStream    *stream;
    uint32_t         width;
    uint32_t         height;
    GstVideoFormat   format;
    gpointer         user_data;
    GDestroyNotify   destroy_notify;
};

GstBuffer*
ems_gst_buffer_new_wrapped_cuda(const struct ems_gst_buffer_new_wrapped_cuda_info *info,
                                const struct vk_cuda_image *vkc_image);

/*
 *
 * Loads/sets GstCuda dylib function pointers and 
 * creates a CudaContext matching a given Vulkan Device uuid.
 *
 */
GstCudaContext*
ems_gst_load_cuda_context(const struct xrt_uuid *vk_device_uuid);

bool
ems_vulkan_cuda_test(const struct xrt_uuid *vk_device_uuid, struct vk_bundle *vk);

#ifdef __cplusplus
}
#endif
