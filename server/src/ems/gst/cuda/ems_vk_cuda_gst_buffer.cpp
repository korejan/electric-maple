// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief source - Vulkan-CUDA - GstBuffer wrapping helper.
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup aux_util
 */
#define GST_USE_UNSTABLE_API
#include "ems_vk_cuda_gst_buffer.h"
#include "ems_vk_cuda_image.h"
#include <gst/video/gstvideoutils.h>
#include <gst/video/gstvideometa.h>

GstBuffer*
ems_gst_buffer_new_wrapped_cuda(const ems_gst_buffer_new_wrapped_cuda_info *info,
                                const struct vk_cuda_image *vkc_image) {
    if (info == nullptr || vkc_image == nullptr)
        return nullptr;
    if (vkc_image->cuda.array == 0)
        return nullptr;

    GstVideoInfo gst_video_info = {};
    gst_video_info_init(&gst_video_info);
    gst_video_info_set_format(
        &gst_video_info,
        info->format,
        info->width,
        info->height
    );
    GST_VIDEO_INFO_PLANE_STRIDE(&gst_video_info, 0) = static_cast<int>(info->width * 4);
    GST_VIDEO_INFO_PLANE_OFFSET(&gst_video_info, 0) = 0;
    GST_VIDEO_INFO_SIZE(&gst_video_info) = vkc_image->base.size;

    GstMemory *cuda_memory_wrapped = gst_cuda_allocator_alloc_wrapped(
        info->allocator,
        info->context,
        info->stream,
        &gst_video_info,
        (CUdeviceptr)vkc_image->cuda.array,
        info->user_data,
        info->destroy_notify
    );
    if (cuda_memory_wrapped == nullptr) {
        return nullptr;
    }
    
    GstBuffer *buffer = gst_buffer_new();
    gst_buffer_append_memory(buffer, cuda_memory_wrapped);

	gsize offsets[4] = {0, 0, 0, 0};
    gst_buffer_add_video_meta_full(buffer, GST_VIDEO_FRAME_FLAG_NONE,
                            GST_VIDEO_INFO_FORMAT (&gst_video_info), GST_VIDEO_INFO_WIDTH (&gst_video_info),
                            GST_VIDEO_INFO_HEIGHT (&gst_video_info), GST_VIDEO_INFO_N_PLANES(&gst_video_info),
                            offsets, gst_video_info.stride);
    return buffer;
}

GstCudaContext*
ems_gst_load_cuda_context(const struct xrt_uuid *vk_device_uuid) {
    if (!gst_cuda_load_library()) {
        return nullptr;
    }
    ems_vk_cuda_device cuda_device = {};
    if (!ems_find_matching_cuda_device(vk_device_uuid, &cuda_device)) {
        return nullptr;
    }
    return gst_cuda_context_new(cuda_device.device_id);
}

#define APP_VIEW_W (1680) // 2^4 * 3 * 5 * 7
#define APP_VIEW_H (1760) // 2^5 * 5 * 11
#define READBACK_W_HALF (4 * APP_VIEW_W / 5)
#define READBACK_W (READBACK_W_HALF * 2)
#define READBACK_H (4 * APP_VIEW_H / 5)

bool
ems_vulkan_cuda_test(const struct xrt_uuid *vk_device_uuid, struct vk_bundle *vk) {

    GstCudaContext *cudaCtx = ems_gst_load_cuda_context(vk_device_uuid);
    if (cudaCtx == nullptr) {
        return false;
    }

    struct vk_cuda_image cuda_vk_image = {};
    ems_create_cuda_image_info info = {
        .vk = vk,
        .alloc_callbacks = nullptr,
        .size = {READBACK_W, READBACK_H},
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .flags = 0,
        .image_tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
    if (ems_create_cuda_vk_image(&info, &cuda_vk_image) != VK_SUCCESS ||
        cuda_vk_image.cuda.array == 0) {
        return false;
    }

    struct ems_gst_buffer_new_wrapped_cuda_info wrapped_buff_info = {
        .allocator = nullptr,
        .context = cudaCtx,
        .stream = nullptr,
        .width = info.size.width,
        .height = info.size.height,
        .format = GST_VIDEO_FORMAT_RGBA,
        .user_data = nullptr,
        .destroy_notify = [](void*){ /* noop destroy */ },
    };
    GstBuffer *new_gst_buffer = ems_gst_buffer_new_wrapped_cuda(&wrapped_buff_info, &cuda_vk_image);
    return new_gst_buffer != nullptr;
}
