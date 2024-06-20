// Copyright 2019-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Semi internal structs for gstreamer code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup aux_util
 */

#pragma once
#define GST_USE_UNSTABLE_API
#include "gst/cuda/ems_vk_cuda_image_pool.h"
#include "xrt/xrt_frame.h"
#include <gst/cuda/gstcuda.h>

typedef struct _GstElement GstElement;


#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * Pipeline
 *
 */

/*!
 * A pipeline from which you can create one or more @ref gstreamer_sink from.
 *
 * @implements xrt_frame_node
 */
struct gstreamer_pipeline
{
	struct xrt_frame_node node;

	struct xrt_frame_context *xfctx;

	GstElement *pipeline;
};


/*
 *
 * Sink
 *
 */

/*!
 * An @ref xrt_frame_sink that uses appsrc.
 *
 * @implements xrt_frame_sink
 * @implements xrt_frame_node
 */
struct ems_gstreamer_src
{
	//! The base structure exposing the sink interface.
	struct xrt_frame_sink base;

	//! A sink can expose multie @ref xrt_frame_sink but only one node.
	struct xrt_frame_node node;

	//! Pipeline this sink is producing frames into.
	struct gstreamer_pipeline *gp;

	//! hw-buffer pool
	struct ems_vk_cuda_image_pool *vk_cuda_image_pool;

	//! vulkan device UUID for hw-accel interop, e.g. CudaContext.
	xrt_uuid_t vk_device_uuid;

	GstCudaContext* gst_cuda_context;

	//! Offset applied to timestamps given to GStreamer.
	uint64_t offset_ns;

	//! Last sent timestamp, used to calculate duration.
	uint64_t timestamp_ns;

	//! Cached appsrc element.
	GstElement *appsrc;
};


#ifdef __cplusplus
}
#endif
