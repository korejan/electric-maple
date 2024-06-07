// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

/*!
 * @file
 * @brief Main file for WebRTC client.
 * @author Moshi Turner <moses@collabora.com>
 * @author Rylie Pavlik <rpavlik@collabora.com>
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 */
#include "EglData.hpp"
#include "em/em_egl.h"
#include "em/em_remote_experience.h"
#include "em/render/xr_platform_deps.h"


#include "em/em_app_log.h"
#include "em/em_connection.h"
#include "em/em_stream_client.h"
#include "em/gst_common.h"
#include "em/render/render.hpp"

#include "os/os_time.h"
#include "util/u_time.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>

#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#include <gst/gst.h>

#include <memory>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <pthread.h>
#include <jni.h>
#include <errno.h>
#include <stdbool.h>
#include <thread>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <assert.h>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include <sys/system_properties.h>


#define XR_LOAD(fn) xrGetInstanceProcAddr(state.instance, #fn, (PFN_xrVoidFunction *)&fn);

namespace {

struct em_state
{
	bool connected;

	XrInstance instance;
	XrSystemId system;
	XrSession session;
	XrSessionState sessionState;

	uint32_t width;
	uint32_t height;

	EmConnection *connection;
};

em_state state = {};

void
onAppCmd(struct android_app *app, int32_t cmd)
{
	switch (cmd) {
	case APP_CMD_START: ALOGE("APP_CMD_START"); break;
	case APP_CMD_RESUME: ALOGE("APP_CMD_RESUME"); break;
	case APP_CMD_PAUSE: ALOGE("APP_CMD_PAUSE"); break;
	case APP_CMD_STOP:
		ALOGE("APP_CMD_STOP - shutting down connection");
		em_connection_disconnect(state.connection);
		state.connected = false;
		break;
	case APP_CMD_DESTROY: ALOGE("APP_CMD_DESTROY"); break;
	case APP_CMD_INIT_WINDOW: ALOGE("APP_CMD_INIT_WINDOW"); break;
	case APP_CMD_TERM_WINDOW:
		ALOGE("APP_CMD_TERM_WINDOW - shutting down connection");
		em_connection_disconnect(state.connection);
		state.connected = false;
		break;
	}
}

/**
 * Poll for Android and OpenXR events, and handle them
 *
 * @param state app state
 *
 * @return true if we should go to the render code
 */
bool
poll_events(struct android_app *app, struct em_state &state)
{

	// Poll Android events
	for (;;) {
		int events;
		struct android_poll_source *source;
		bool wait = !app->window || app->activityState != APP_CMD_RESUME;
		int timeout = wait ? -1 : 0;
		if (ALooper_pollAll(timeout, NULL, &events, (void **)&source) >= 0) {
			if (source) {
				source->process(app, source);
			}

			if (timeout == 0 && (!app->window || app->activityState != APP_CMD_RESUME)) {
				break;
			}
		} else {
			break;
		}
	}

	// Poll OpenXR events
	XrResult result = XR_SUCCESS;
	XrEventDataBuffer buffer;
	buffer.type = XR_TYPE_EVENT_DATA_BUFFER;
	buffer.next = NULL;

	while (xrPollEvent(state.instance, &buffer) == XR_SUCCESS) {
		if (buffer.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
			XrEventDataSessionStateChanged *event = (XrEventDataSessionStateChanged *)&buffer;

			switch (event->state) {
			case XR_SESSION_STATE_IDLE: ALOGI("OpenXR session is now IDLE"); break;
			case XR_SESSION_STATE_READY: {
				ALOGI("OpenXR session is now READY, beginning session");
				XrSessionBeginInfo beginInfo = {};
				beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
				beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

				result = xrBeginSession(state.session, &beginInfo);

				if (XR_FAILED(result)) {
					ALOGI("Failed to begin OpenXR session (%d)", result);
				}
			} break;
			case XR_SESSION_STATE_SYNCHRONIZED: ALOGI("OpenXR session is now SYNCHRONIZED"); break;
			case XR_SESSION_STATE_VISIBLE: ALOGI("OpenXR session is now VISIBLE"); break;
			case XR_SESSION_STATE_FOCUSED: ALOGI("OpenXR session is now FOCUSED"); break;
			case XR_SESSION_STATE_STOPPING:
				ALOGI("OpenXR session is now STOPPING");
				xrEndSession(state.session);
				break;
			case XR_SESSION_STATE_LOSS_PENDING: ALOGI("OpenXR session is now LOSS_PENDING"); break;
			case XR_SESSION_STATE_EXITING: ALOGI("OpenXR session is now EXITING"); break;
			default: break;
			}

			state.sessionState = event->state;
		}

		buffer.type = XR_TYPE_EVENT_DATA_BUFFER;
	}

	// If session isn't ready, return. We'll be called again and will poll events again.
	if (state.sessionState < XR_SESSION_STATE_READY) {
		ALOGI("Waiting for session ready state!");
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(100ms);
		return false;
	}

	return true;
}

void
connected_cb(EmConnection *connection, struct em_state *state)
{
	ALOGI("%s: Got signal that we are connected!", __FUNCTION__);

	state->connected = true;
}

} // namespace

static GMutex property_read_mutex;
static std::string property_result;
static bool property_received = false;
#define WEBSOCKET_URI_PROPERTY_NAME "debug.electric_maple.websocket_uri"

void property_read_cb(void* cookie, const char*, const char* value, unsigned int serial) {
	(void) cookie;
	(void) serial;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&property_read_mutex);
	(void) locker;
	property_result = std::string(value);
	property_received = true;
	ALOGD("Got %s property: %s", WEBSOCKET_URI_PROPERTY_NAME, value);
}

std::string
read_websocket_uri_property(uint32_t timeout_ms)
{
	g_mutex_init(&property_read_mutex);

	const prop_info *info = __system_property_find(WEBSOCKET_URI_PROPERTY_NAME);
	if (info != nullptr) {
		__system_property_read_callback(info, property_read_cb, 0);

		// HACK: Making this synchronous
		auto start = std::chrono::steady_clock::now();
		uint32_t wait_duration_ms = 0;

		while (wait_duration_ms < timeout_ms) {
			g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&property_read_mutex);
			(void)locker;
			if (property_received) {
				return property_result;
			}
			auto now = std::chrono::steady_clock::now();
			wait_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
		}
		ALOGW("Timeout of %dms reached for reading %s", timeout_ms, WEBSOCKET_URI_PROPERTY_NAME);
	} else {
		ALOGD("%s not set.", WEBSOCKET_URI_PROPERTY_NAME);
	}

	return "";
}

static std::vector<std::string>
get_supported_xr_extensions() {

	std::vector<std::string> results;
	
	const auto add_extensions = [&](const char* layerName) {
		uint32_t instanceExtensionCount = 0;
		if (XR_FAILED(xrEnumerateInstanceExtensionProperties(layerName, 0, &instanceExtensionCount, nullptr))) {
			return;
		}

		std::vector<XrExtensionProperties> extensions(instanceExtensionCount, {
			.type = XR_TYPE_EXTENSION_PROPERTIES,
			.next = nullptr
		});
		if (XR_FAILED(xrEnumerateInstanceExtensionProperties(layerName, (uint32_t)extensions.size(), &instanceExtensionCount,
			extensions.data()))) {
			return;
		}

		for (const XrExtensionProperties& extension : extensions) {
			results.push_back(extension.extensionName);
		}
	};

	// add non-layer extensions (layerName==nullptr).
	add_extensions(nullptr);

	// add layers extensions.
	{
		uint32_t layerCount = 0;
		if (XR_FAILED(xrEnumerateApiLayerProperties(0, &layerCount, nullptr))) {
			goto lastStep;
		}
		std::vector<XrApiLayerProperties> layers(layerCount, {
			.type = XR_TYPE_API_LAYER_PROPERTIES,
			.next = nullptr
		});
		if (XR_FAILED(xrEnumerateApiLayerProperties((uint32_t)layers.size(), &layerCount, layers.data()))) {
			goto lastStep;
		}
		for (const XrApiLayerProperties& layer : layers) {
			add_extensions(layer.layerName);
		}
	}
lastStep:	
	std::sort(results.begin(), results.end());
	return results;
}

void
android_main(struct android_app *app)
{

	// Debugging gstreamer.
	// GST_DEBUG = *:3 will give you ONLY ERROR-level messages.
	// GST_DEBUG = *:6 will give you ALL messages (make sure you BOOST your android-studio's
	// Logcat buffer to be able to capture everything gstreamer's going to spit at you !
	// in Tools -> logcat -> Cycle Buffer Size (I set it to 102400 KB).

	// TODO: Make configurable via `adb shell setprop`
	constexpr const char *gst_debug_string = "*:2,amc:0";
//	constexpr const char *gst_debug_string = "*:3";
//	constexpr const char *gst_debug_string = "*ssl*:9,*tls*:9,*webrtc*:9";
//	constexpr const char *gst_debug_string = "GST_CAPS:5";
//	constexpr const char *gst_debug_string = "*:2,webrtc*:9,sctp*:9,dtls*:9,amcvideodec:9";
//	constexpr const char *gst_debug_string = "*:2, default:5";

	JNIEnv *env = nullptr;
	(*app->activity->vm).AttachCurrentThread(&env, NULL);
	app->onAppCmd = onAppCmd;

	auto initialEglData = std::make_unique<EglData>();

	//
	// Normal OpenXR app startup
	//

	// Initialize OpenXR loader
	PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = NULL;
	XR_LOAD(xrInitializeLoaderKHR);
	XrLoaderInitInfoAndroidKHR loaderInfo = {
	    .type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
	    .applicationVM = app->activity->vm,
	    .applicationContext = app->activity->clazz,
	};

	XrResult result = xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR *)&loaderInfo);

	if (XR_FAILED(result)) {
		ALOGE("Failed to initialize OpenXR loader\n");
		return;
	}

	// Create OpenXR instance
	std::vector<const char *> requiredExtensions = {
		// XR_KHR_ exts
		XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
		XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
		XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME,
		// XR_EXT_ exts
		// ...
		// XR_Vendor_ specific exts
		// ...
	};

	std::array<const char*, 0> optionalExtensions = {
		// XR_KHR_ exts
		// ...
		// XR_EXT_ exts
		// ...
		// XR_Vendor_ specific exts
	};

	const auto supportedXrExtensions = get_supported_xr_extensions();
	
	for (const auto optionalExtName : optionalExtensions) {
		const auto itr = std::find(supportedXrExtensions.begin(), supportedXrExtensions.end(), optionalExtName);
		if (itr != supportedXrExtensions.end()) {
			requiredExtensions.push_back(optionalExtName);
		}
	}

	XrInstanceCreateInfoAndroidKHR androidInfo = {};
	androidInfo.type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR;
	androidInfo.applicationActivity = app->activity->clazz;
	androidInfo.applicationVM = app->activity->vm;

	XrInstanceCreateInfo instanceInfo = {};
	instanceInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.next = &androidInfo;

	strncpy(instanceInfo.applicationInfo.engineName, "N/A", XR_MAX_APPLICATION_NAME_SIZE - 1);
	instanceInfo.applicationInfo.engineName[XR_MAX_APPLICATION_NAME_SIZE - 1] = '\0';

	strncpy(instanceInfo.applicationInfo.applicationName, "N/A", XR_MAX_APPLICATION_NAME_SIZE - 1);
	instanceInfo.applicationInfo.applicationName[XR_MAX_APPLICATION_NAME_SIZE - 1] = '\0';

	instanceInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	instanceInfo.enabledExtensionCount = (std::uint32_t)requiredExtensions.size();
	instanceInfo.enabledExtensionNames = requiredExtensions.data();

	result = xrCreateInstance(&instanceInfo, &state.instance);

	if (XR_FAILED(result)) {
		ALOGE("Failed to initialize OpenXR instance\n");
		return;
	}

	// OpenXR system

	XrSystemGetInfo systemInfo = {.type = XR_TYPE_SYSTEM_GET_INFO,
	                              .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};

	result = xrGetSystem(state.instance, &systemInfo, &state.system);

	uint32_t viewConfigurationCount;
	XrViewConfigurationType viewConfigurations[2];
	result =
	    xrEnumerateViewConfigurations(state.instance, state.system, 2, &viewConfigurationCount, viewConfigurations);

	if (XR_FAILED(result)) {
		ALOGE("Failed to enumerate view configurations\n");
		return;
	}

	XrViewConfigurationView viewInfo[2] = {};
	viewInfo[0].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	viewInfo[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;

	uint32_t viewCount = 0;
	xrEnumerateViewConfigurationViews(state.instance, state.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
	                                  &viewCount, NULL);
	result = xrEnumerateViewConfigurationViews(state.instance, state.system,
	                                           XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, viewInfo);

	if (XR_FAILED(result) || viewCount != 2) {
		ALOGE("Failed to enumerate view configuration views\n");
		return;
	}

	state.width = viewInfo[0].recommendedImageRectWidth;
	state.height = viewInfo[0].recommendedImageRectHeight;

	ALOGI("Got recommended eye dimensions: %dx%d", state.width, state.height);

	// OpenXR session
	ALOGI("FRED: Creating OpenXR session...");
	PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGLESGraphicsRequirementsKHR = NULL;
	XR_LOAD(xrGetOpenGLESGraphicsRequirementsKHR);
	XrGraphicsRequirementsOpenGLESKHR graphicsRequirements = {.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
	xrGetOpenGLESGraphicsRequirementsKHR(state.instance, state.system, &graphicsRequirements);

	XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding = {
	    .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
	    .display = initialEglData->display,
	    .config = initialEglData->config,
	    .context = initialEglData->context,
	};

	XrSessionCreateInfo sessionInfo = {
	    .type = XR_TYPE_SESSION_CREATE_INFO, .next = &graphicsBinding, .systemId = state.system};

	result = xrCreateSession(state.instance, &sessionInfo, &state.session);

	if (XR_FAILED(result)) {
		ALOGE("ERROR: Failed to create OpenXR session (%d)\n", result);
		return;
	}

	EmEglMutexIface *egl_mutex = em_egl_mutex_create(initialEglData->display, initialEglData->context);

	//
	// End of normal OpenXR app startup
	//

	//
	// Start of remote-rendering-specific code
	//

	// Set up gstreamer
	gst_init(0, NULL);

	// Does this need to be set after gst-init?
	gst_debug_set_threshold_from_string(gst_debug_string, true);

	// Set up our own objects
	ALOGI("%s: creating stream client object", __FUNCTION__);
	EmStreamClient *stream_client = em_stream_client_new();

	ALOGI("%s: telling stream client about EGL", __FUNCTION__);
	// retaining ownership
	em_stream_client_set_egl_context(stream_client, egl_mutex, false, initialEglData->surface);

	// Read debug.electric_maple.websocket_uri
	std::string websocket_uri_property = read_websocket_uri_property(5000);

	ALOGI("%s: creating connection object", __FUNCTION__);
	if (!websocket_uri_property.empty()) {
		state.connection = g_object_ref_sink(em_connection_new(websocket_uri_property.c_str()));
	} else {
		state.connection = g_object_ref_sink(em_connection_new_localhost());
	}

	g_signal_connect(state.connection, "connected", G_CALLBACK(connected_cb), &state);

	ALOGI("%s: starting connection", __FUNCTION__);
	em_connection_connect(state.connection);

	ALOGI("%s: starting stream client mainloop thread", __FUNCTION__);
	em_stream_client_spawn_thread(stream_client, state.connection);

	const EmXrInfo em_xr_info = {
		.instance = state.instance,
		.session  = state.session,
		.eye_extents = {static_cast<int32_t>(state.width), static_cast<int32_t>(state.height)},
		.enabled_extensions_count = (uint32_t)requiredExtensions.size(),
		.enabled_extensions = requiredExtensions.data(),
	};
	EmRemoteExperience *remote_experience =
	    em_remote_experience_new(state.connection, stream_client, &em_xr_info);
	if (!remote_experience) {
		ALOGE("%s: Failed during remote experience init.", __FUNCTION__);
		return;
	}

	//
	// End of remote-rendering-specific setup, into main loop
	//

	// Main rendering loop.
	ALOGI("DEBUG: Starting main loop.\n");
	while (!app->destroyRequested) {
		if (poll_events(app, state)) {
			em_remote_experience_poll_and_render_frame(remote_experience);
		}
	}

	ALOGI("DEBUG: Exited main loop, cleaning up.\n");
	//
	// Clean up RR structures
	//

	g_clear_object(&state.connection);
	// without gobject for stream client, the EmRemoteExperience takes ownership
	// g_clear_object(&stream_client);

	em_remote_experience_destroy(&remote_experience);

	em_egl_mutex_destroy(&egl_mutex);

	//
	// End RR cleanup
	//

	initialEglData = nullptr;

	(*app->activity->vm).DetachCurrentThread();
}
