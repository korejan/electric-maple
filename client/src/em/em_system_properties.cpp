// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal source for android system properties by the ElectricMaple XR streaming solution
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup em_client
 */
#include "em_system_properties.hpp"
#include "em_app_log.h"

#include <atomic>
#include <cstddef>
#include <array>
#include <sstream>

#include <sys/system_properties.h>
#include <gst/gstutils.h>
#include <gst/gstobject.h>

namespace em {

static GMutex property_read_mutex;
static std::string property_result;
static std::atomic_bool property_received = false;

static void
property_read_cb(void* cookie, const char*, const char* value, unsigned int serial) {
	const char* property_name = reinterpret_cast<const char*>(cookie);
	(void) serial;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&property_read_mutex);
	(void) locker;
	property_result = std::string(value);
	property_received = true;
	ALOGD("Got %s property: %s", property_name, value);
}

std::string
read_system_property(const char* property_name, uint32_t timeout_ms)
{
	if (property_name == nullptr) {
		ALOGW("read_system_property: \"property_name\" argument is null.");
		return {};
	}

	g_mutex_init(&property_read_mutex);

	const prop_info *info = __system_property_find(property_name);
	if (info != nullptr) {
		__system_property_read_callback(info, property_read_cb, (void*)property_name);

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
		ALOGW("Timeout of %dms reached for reading %s", timeout_ms, property_name);
	} else {
		ALOGD("%s not set.", property_name);
	}

	return {};
}

std::optional<float>
read_system_property_float(const char* property_name, uint32_t timeout_ms) {
	const auto val_str = read_system_property(property_name, timeout_ms);
	if (val_str.length() == 0)
		return std::nullopt;
	float ret = 0;
	std::istringstream oss(val_str);
	if (!(oss >> ret))
		return std::nullopt;
	return ret;
}

std::optional<xrt_vec3>
read_system_property_vec3f(const char* property_name, uint32_t timeout_ms) {
	const auto val_str = read_system_property(property_name, timeout_ms);
	if (val_str.length() == 0)
		return std::nullopt;

	std::array<float,3> vals;
	std::istringstream iss{val_str};
	for (std::size_t idx = 0; idx < 3; ++idx) {
		std::string elem;
		if (!std::getline(iss, elem, ',')) {
			return std::nullopt;
		}
		std::istringstream iss2{elem};
		if (!(iss2 >> vals[idx])) {
			return std::nullopt;
		}
	}
	return xrt_vec3 {vals[0],vals[1],vals[2]};
}

}