// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  @ref xrt_frame_sink that does gst things.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once

#include <glib.h>

#include "gst/cuda/ems_vk_cuda_image.h"
#include "xrt/xrt_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xrt_uuid;
struct vk_cuda_image;
struct ems_vk_cuda_image_pool;
struct ems_gstreamer_src;
struct gstreamer_pipeline;

void
ems_gstreamer_src_push_vk_cuda_image(struct ems_gstreamer_src *gs,
                                     struct vk_cuda_image *vkci,
                                     GBytes *downMsg_bytes,
                                     uint64_t xtimestamp_ns);

void
ems_gstreamer_src_push_frame(struct ems_gstreamer_src *gs, struct xrt_frame *xf, GBytes *downMsg_bytes);

void
ems_gstreamer_src_create_with_pipeline(struct gstreamer_pipeline *gp,
                                       uint32_t width,
                                       uint32_t height,
                                       enum xrt_format format,
                                       const char *appsrc_name,
                                       const struct xrt_uuid *vk_device_uuid,
                                       struct ems_vk_cuda_image_pool *image_pool,
                                       struct ems_gstreamer_src **out_gs,
                                       struct xrt_frame_sink **out_xfs);


#ifdef __cplusplus
}
#endif
