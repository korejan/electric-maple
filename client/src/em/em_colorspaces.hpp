// Copyright 2024, Collabora, Ltd.
//
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Internal header for colorspace transforms used by the ElectricMaple XR streaming solution
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 * @ingroup em_client
 */
#pragma once
#include <openxr/openxr.h>
#include "math/m_api.h"

namespace em {

inline constexpr const xrt_matrix_4x4 NON_LINEAR_SRGB_TO_YUV_BT709_MAT = {
    .v = {
        0.2126, -0.1146,  0.5000, 0.0,
        0.7152, -0.3854, -0.4542, 0.0,
        0.0722,  0.5000, -0.0458, 0.0,
        0.0,     0.5,     0.5,    1.0
    }
};

inline constexpr const xrt_matrix_4x4 NON_LINEAR_SRGB_TO_YUV_BT2020_MAT = {
    .v = {
        0.2627, -0.1396,  0.5000, 0.0,
        0.6780, -0.3604, -0.0416, 0.0,
        0.0593,  0.5000, -0.4584, 0.0,
        0.0,     0.5,     0.5,    1.0
    }
};

inline constexpr const xrt_matrix_4x4 LINEAR_SRGB_TO_YUV_BT709_MAT = {
    .v = {
        0.2126, -0.09991,  0.615,   0.0,
        0.7152, -0.33609, -0.55861, 0.0,
        0.0722,  0.436,   -0.05639, 0.0,
        0.0,     0.5,      0.5,     1.0
    },
};

inline constexpr const xrt_matrix_4x4 LINEAR_SRGB_TO_YUV_BT2020_MAT = {
    .v = {
        0.2627, -0.13963,  0.5,    0.0,
        0.6780, -0.36037, -0.3607, 0.0,
        0.0593,  0.5,     -0.1393, 0.0,
        0.0,     0.5,      0.5,    1.0
    },
};

inline xrt_vec3 srgb_to_yuv(const xrt_matrix_4x4& mat, const xrt_vec3& x) {
    xrt_vec3 yuv = {0,0,0};
    math_matrix_4x4_transform_vec3(&mat, &x, &yuv);
    return yuv;
}

inline xrt_vec3 non_linear_srgb_to_yuv_b709(const xrt_vec3& x) {
    return srgb_to_yuv(NON_LINEAR_SRGB_TO_YUV_BT709_MAT, x);
}

inline xrt_vec3 non_linear_srgb_to_yuv_b2020(const xrt_vec3& x) {
    return srgb_to_yuv(NON_LINEAR_SRGB_TO_YUV_BT2020_MAT, x);
}

inline xrt_vec3 linear_srgb_to_yuv_b709(const xrt_vec3& x) {
    return srgb_to_yuv(LINEAR_SRGB_TO_YUV_BT709_MAT, x);
}

inline xrt_vec3 linear_srgb_to_yuv_b2020(const xrt_vec3& x) {
    return srgb_to_yuv(LINEAR_SRGB_TO_YUV_BT2020_MAT, x);
}

}