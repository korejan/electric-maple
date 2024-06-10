// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

/*!
 * @file
 * @brief Very simple GLES3 renderer for WebRTC client.
 * @author Moshi Turner <moses@collabora.com>
 * @author Rylie Pavlik <rpavlik@collabora.com>
 * @author Korcan Hussein <korcan.hussein@collabora.com>
 */

#include "render.hpp"

#include "GLDebug.h"
#include "GLError.h"
#include "../em_app_log.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <openxr/openxr.h>
#include <stdexcept>
#include <array>

// Vertex shader source code
static constexpr const GLchar *vertexShaderSource = R"(
    #version 300 es
    in vec3 position;
    in vec2 uv;
    out vec2 frag_uv;

    void main() {
        gl_Position = vec4(position, 1.0);
        frag_uv = uv;
    }
)";

static constexpr const GLchar *streamFragBaseShader = R"(
	#version 300 es
    #extension GL_OES_EGL_image_external : require
    #extension GL_OES_EGL_image_external_essl3 : require
    precision mediump float;

    in vec2 frag_uv;
    out vec4 frag_color;
    uniform samplerExternalOES textureSampler;
)";

// Fragment shader source code
static constexpr const GLchar *fragmentShaderSource = R"(
    void main() {
        frag_color = texture(textureSampler, frag_uv);
    }
)";

/*!
 * AdditiveSimFragShader shader emulates the behaviour of XR_ENVIRONMENT_BLEND_MODE_ADDITIVE
 * for client runtimes that do not support this mode but supports
 * XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND or passthrough composition layers
 * with alpha blending in the vendor extensions (e.g. XR_FB_passthrough) case.
 */
static constexpr const GLchar *AdditiveSimFragShader = R"(

	const mat4 LINEAR_SRGB_TO_YUV_BT709_MAT = mat4(
		0.2126, -0.09991,  0.615,   0.0,
		0.7152, -0.33609, -0.55861, 0.0,
		0.0722,  0.436,   -0.05639, 0.0,
		0.0,     0.5,      0.5,     1.0
	);
	const mat4 LINEAR_SRGB_TO_YUV_BT2020_MAT = mat4(
		0.2627, -0.13963,  0.5,    0.0,
		0.6780, -0.36037, -0.3607, 0.0,
		0.0593,  0.5,     -0.1393, 0.0,
		0.0,     0.5,      0.5,    1.0
	);
	const mat4 NON_LINEAR_SRGB_TO_YUV_BT709_MAT = mat4(
		0.2126, -0.1146,  0.5000, 0.0,
		0.7152, -0.3854, -0.4542, 0.0,
		0.0722,  0.5000, -0.0458, 0.0,
		0.0,     0.5,     0.5,    1.0
	);
	const mat4 NON_LINEAR_SRGB_TO_YUV_BT2020_MAT = mat4(
		0.2627, -0.1396,  0.5000, 0.0,
		0.6780, -0.3604, -0.0416, 0.0,
		0.0593,  0.5000, -0.4584, 0.0,
		0.0,     0.5,     0.5,    1.0
	);

	uniform vec3 keyColor; // format & colorspace: YUV_BT2020
	uniform float keyThreshold;

    void main() {
        vec3 color  = texture(textureSampler, frag_uv).rgb;
		vec4 yuv    = LINEAR_SRGB_TO_YUV_BT2020_MAT * vec4(color, 1.0);
        float dist  = distance(keyColor.yz, yuv.yz);
		float alpha = (dist < keyThreshold) ? 0.0 : 1.0;
        frag_color  = vec4(color, alpha);
    }
)";

// Function to check shader compilation errors
void
checkShaderCompilation(GLuint shader)
{
	GLint success;
	GLchar infoLog[512];
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(shader, sizeof(infoLog), NULL, infoLog);
		ALOGE("Shader compilation failed: %s\n", infoLog);
	}
}

// Function to check shader program linking errors
void
checkProgramLinking(GLuint program)
{
	GLint success;
	GLchar infoLog[512];
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(program, sizeof(infoLog), NULL, infoLog);
		ALOGE("Shader program linking failed: %s\n", infoLog);
	}
}

void
Renderer::setupShaders()
{
	// Compile the vertex shader
	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
	glCompileShader(vertexShader);
	checkShaderCompilation(vertexShader);

	// Compile the fragment shader
	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	std::array<const GLchar *, 2> shaderSrcStrings = {
		streamFragBaseShader,
		fragmentShaderSource,
	};
	glShaderSource(fragmentShader, shaderSrcStrings.size(), shaderSrcStrings.data(),nullptr);
	glCompileShader(fragmentShader);
	checkShaderCompilation(fragmentShader);

	// Compile AdditiveSim fragment shader
	GLuint additiveSimFragShader = glCreateShader(GL_FRAGMENT_SHADER);
	shaderSrcStrings = {
		streamFragBaseShader,
		AdditiveSimFragShader,
	};
	glShaderSource(additiveSimFragShader, shaderSrcStrings.size(), shaderSrcStrings.data(), nullptr);
	glCompileShader(additiveSimFragShader);
	checkShaderCompilation(additiveSimFragShader);

	// Create and link the shader program
	const GLuint program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);
	checkProgramLinking(program);

	const GLuint additiveSimProgram = glCreateProgram();
	glAttachShader(additiveSimProgram, vertexShader);
	glAttachShader(additiveSimProgram, additiveSimFragShader);
	glLinkProgram(additiveSimProgram);
	checkProgramLinking(additiveSimProgram);

	// Clean up the shaders as they're no longer needed
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	glDeleteShader(additiveSimFragShader);

	const GLchar* const samplerName = "textureSampler";
	programs = {
		Program {
			.id = program,
			.textureSamplerLocation = glGetUniformLocation(program, samplerName),
		},
		Program {
			.id = additiveSimProgram,
			.textureSamplerLocation = glGetUniformLocation(additiveSimProgram, samplerName),
			.keyColorLocation       = glGetUniformLocation(additiveSimProgram, "keyColor"),
			.keyThresholdLocation   = glGetUniformLocation(additiveSimProgram, "keyThreshold"),
		}
	};
}

struct TextureCoord
{
	float u;
	float v;
};
struct Vertex
{
	XrVector3f pos;
	TextureCoord texcoord;
};
static constexpr size_t kVertexBufferStride = sizeof(Vertex);

static_assert(kVertexBufferStride == 5 * sizeof(GLfloat), "3 position coordinates and u,v");

void
Renderer::setupQuadVertexData()
{
	// Set up the quad vertex data
	static constexpr Vertex quadVertices[] = {
	    {{-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
	    {{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
	    {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
	    {{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
	};

	glGenVertexArrays(1, &quadVAO);
	glGenBuffers(1, &quadVBO);

	glBindVertexArray(quadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

	static constexpr size_t stride = sizeof(Vertex) / sizeof(GLfloat);
	static_assert(stride == 5, "3 position coordinates and u,v");
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kVertexBufferStride, (GLvoid *)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, kVertexBufferStride, (GLvoid *)offsetof(Vertex, texcoord));
	glEnableVertexAttribArray(1);

	glBindVertexArray(0);
}

Renderer::~Renderer()
{
	reset();
}

void
Renderer::setupRender()
{


	registerGlDebugCallback();
	setupShaders();
	setupQuadVertexData();
}

void
Renderer::reset()
{
	for (auto& program : programs) {
		if (program.id != 0) {
			glDeleteProgram(program.id);
			program = {};
		}
	}

	if (quadVAO != 0) {
		glDeleteVertexArrays(1, &quadVAO);
		quadVAO = 0;
	}
	if (quadVBO != 0) {
		glDeleteBuffers(1, &quadVBO);
		quadVBO = 0;
	}
}

void
Renderer::draw(const Renderer::DrawInfo& drawInfo) const
{
	//    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	// Use the shader program
	const auto& program = programs[drawInfo.alpha_for_additive.enable];
	glUseProgram(program.id);

	// Bind the texture
	glActiveTexture(GL_TEXTURE0);
	// glBindTexture(GL_TEXTURE_2D, texture);
	glBindTexture(drawInfo.texture_target, drawInfo.texture);
	glUniform1i(program.textureSamplerLocation, 0);
	
	if (drawInfo.alpha_for_additive.enable) {
		glUniform3fv(program.keyColorLocation, 1, drawInfo.alpha_for_additive.key_color);
		glUniform1f(program.keyThresholdLocation, drawInfo.alpha_for_additive.key_threshold);
	}

	// Draw the quad
	glBindVertexArray(quadVAO);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glBindVertexArray(0);

	CHECK_GL_ERROR();
#if 0
	GLenum err;
	while ((err = glGetError()) != GL_NO_ERROR) {
		const char *errorStr;
		switch (err) {
		case GL_INVALID_ENUM: errorStr = "GL_INVALID_ENUM"; break;
		case GL_INVALID_VALUE: errorStr = "GL_INVALID_VALUE"; break;
		case GL_INVALID_OPERATION: errorStr = "GL_INVALID_OPERATION"; break;
		case GL_INVALID_FRAMEBUFFER_OPERATION: errorStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
		case GL_OUT_OF_MEMORY: errorStr = "GL_OUT_OF_MEMORY"; break;
		default: errorStr = "Unknown error"; break;
		}
		ALOGE("error! %s", errorStr);
	}
#endif
}
