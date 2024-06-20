# Copyright 2023, Pluto VR, Inc.
#
# SPDX-License-Identifier: BSL-1.0

find_package(CUDAToolkit)
set(CUDA_LIB_LIST)
if (CUDAToolkit_FOUND)
	set(CUDA_LIB_LIST CUDA::cudart_static CUDA::cuda_driver)
	set(ENABLE_CUDA_INTEROP TRUE)
	message(NOTICE "CUDAToolkit found, enabling CUDA buffer interop")
endif()

add_library(ems_gst STATIC ems_gstreamer_pipeline.c ems_signaling_server.c ems_pipeline_args.c ems_gstreamer_src.c
	cuda/ems_vk_cuda_image.cpp
	cuda/ems_vk_cuda_image_pool.cpp
	cuda/ems_vk_cuda_gst_buffer.cpp
)

target_link_libraries(
	ems_gst
	PUBLIC
		CUDA::cudart_static
	PRIVATE
		ems_build_defines
		ems_callbacks
		em_proto
		aux_util
		${GST_LIBRARIES}
		${GST_SDP_LIBRARIES}
		${GST_RTP_LIBRARIES}
		${GST_WEBRTC_LIBRARIES}
		${GST_VIDEO_LIBRARIES}
		${GST_APP_LIBRARIES}
		${GLIB_LIBRARIES}
		${LIBSOUP_LIBRARIES}
		${JSONGLIB_LIBRARIES}
		${GIO_LIBRARIES}
		${GST_CUDA_LIBRARIES}
		${CUDA_LIB_LIST}
	)

target_include_directories(
	ems_gst
	PRIVATE
		${GLIB_INCLUDE_DIRS}
		${GST_INCLUDE_DIRS}
		${LIBSOUP_INCLUDE_DIRS}
		${JSONGLIB_INCLUDE_DIRS}
		${GIO_INCLUDE_DIRS}
		${GST_CUDA_INCLUDE_DIRS}
	)
target_compile_definitions(ems_gst PUBLIC G_LOG_DOMAIN="ElectricMapleServer")
