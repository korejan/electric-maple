// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal header for android system properties by the ElectricMaple XR streaming solution
 * @author Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup em_client
 */
#pragma once
#include <string>
#include <optional>
#undef CLAMP
#include "math/m_api.h"


namespace em {

std::string
read_system_property(const char* property_name, uint32_t timeout_ms);

std::optional<float>
read_system_property_float(const char* property_name, uint32_t timeout_ms);

std::optional<xrt_vec3>
read_system_property_vec3f(const char* property_name, uint32_t timeout_ms);

}
