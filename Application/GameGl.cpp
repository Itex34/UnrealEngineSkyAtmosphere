// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameGl.h"

#if defined(_WIN32)
#include <windows.h>
#endif
#include <GL/gl3w.h>
#include <tinyexr/tinyexr.h>
#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <limits>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	const char* kFullscreenVertexShaderPath = "Resources/glsl/fullscreen_triangle.vert";
	const char* kTransmittanceFragmentShaderPath = "Resources/glsl/transmittance_lut.frag";
	const char* kMultiScatteringComputeShaderPath = "Resources/glsl/new_multi_scattering_lut.comp";
	const char* kSkyViewFragmentShaderPath = "Resources/glsl/skyview_lut.frag";
	const char* kAerialPerspectiveComputeShaderPath = "Resources/glsl/aerial_perspective_volume.comp";
	const char* kTerrainVertexShaderPath = "Resources/glsl/terrain.vert";
	const char* kTerrainFragmentShaderPath = "Resources/glsl/terrain.frag";
	const char* kTerrainShadowFragmentShaderPath = "Resources/glsl/terrain_shadow.frag";
	const char* kRaymarchFragmentShaderPath = "Resources/glsl/render_raymarching_hillaire.frag";
	const char* kPostProcessFragmentShaderPath = "Resources/glsl/postprocess.frag";
	const float kMainCameraFovYDegrees = 66.6f;

	float toneMapPreviewValue(float value, float exposure)
	{
		const float linear = value > 0.0f ? (value * exposure) : 0.0f;
		const float reinhard = linear / (1.0f + linear);
		return std::pow(reinhard, 1.0f / 2.2f);
	}

	std::string readTextFile(const char* path)
	{
		const std::string originalPath(path);
		const std::string candidates[] = {
			originalPath,
			"../" + originalPath,
			"../../" + originalPath,
			"../../../" + originalPath,
			"../../../../" + originalPath,
		};

		for (const std::string& candidate : candidates)
		{
			std::ifstream file(candidate.c_str(), std::ios::in | std::ios::binary);
			if (!file.is_open())
			{
				continue;
			}

			std::ostringstream ss;
			ss << file.rdbuf();
			return ss.str();
		}

		return std::string();
	}

	void showMessageBox(const char* title, const std::string& message)
	{
		std::fprintf(stderr, "%s\n%s\n", title, message.c_str());
#if defined(_WIN32)
		MessageBoxA(nullptr, message.c_str(), title, MB_ICONERROR | MB_OK);
#endif
	}

	void setupEarthAtmosphere(GlAtmosphereInfo& info)
	{
		const float earthBottomRadius = 6360.0f;
		const float earthTopRadius = 6460.0f;
		const float earthRayleighScaleHeight = 8.0f;
		const float earthMieScaleHeight = 1.2f;

		info.bottom_radius = earthBottomRadius;
		info.top_radius = earthTopRadius;
		info.rayleigh_scattering = { 0.005802f, 0.013558f, 0.033100f };
		info.mie_scattering = { 0.003996f, 0.003996f, 0.003996f };
		info.mie_extinction = { 0.004440f, 0.004440f, 0.004440f };
		info.mie_absorption = {
			info.mie_extinction.x - info.mie_scattering.x,
			info.mie_extinction.y - info.mie_scattering.y,
			info.mie_extinction.z - info.mie_scattering.z,
		};
		info.mie_phase_g = 0.8f;
		info.absorption_extinction = { 0.000650f, 0.001881f, 0.000085f };
		info.ground_albedo = { 0.0f, 0.0f, 0.0f };
		info.rayleigh_density_exp_scale = -1.0f / earthRayleighScaleHeight;
		info.mie_density_exp_scale = -1.0f / earthMieScaleHeight;
		info.absorption_layer0_width = 25.0f;
		info.absorption_layer0_linear_term = 1.0f / 15.0f;
		info.absorption_layer0_constant_term = -2.0f / 3.0f;
		info.absorption_layer1_linear_term = -1.0f / 15.0f;
		info.absorption_layer1_constant_term = 8.0f / 3.0f;
	}

	bool loadExrRgba(const char* path, int& outWidth, int& outHeight, std::vector<float>& outPixels, std::string& outError)
	{
		const std::string originalPath(path);
		const std::string candidates[] = {
			originalPath,
			"../" + originalPath,
			"../../" + originalPath,
			"../../../" + originalPath,
			"../../../../" + originalPath,
		};

		int width = 0;
		int height = 0;
		float* rgba = nullptr;
		std::string lastError;
		for (const std::string& candidate : candidates)
		{
			const char* exrError = nullptr;
			const int result = LoadEXR(&rgba, &width, &height, candidate.c_str(), &exrError);
			if (result == TINYEXR_SUCCESS && rgba != nullptr && width > 0 && height > 0)
			{
				outWidth = width;
				outHeight = height;
				const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
				outPixels.assign(rgba, rgba + pixelCount);
				std::free(rgba);
				return true;
			}
			if (exrError != nullptr)
			{
				lastError = exrError;
				FreeEXRErrorMessage(exrError);
			}
		}

		outError = lastError.empty() ? "Heightmap EXR not found." : lastError;
		return false;
	}

	glm::vec3 makeViewDirectionZUpForwardY(float yaw, float pitch)
	{
		const float cp = std::cos(pitch);
		const glm::vec3 dir(
			std::sin(yaw) * cp,
			std::cos(yaw) * cp,
			std::sin(pitch));
		const float len = glm::length(dir);
		return (len > 1e-6f) ? (dir / len) : glm::vec3(0.0f, 1.0f, 0.0f);
	}

}


void GameGl::markLutsDirty()
{
	mLutDirty = true;
	mSkyViewDirty = true;
	mAerialPerspectiveDirty = true;
}

void GameGl::markSkyAndApDirty()
{
	mSkyViewDirty = true;
	mAerialPerspectiveDirty = true;
}

int GameGl::getUniformLocationCached(unsigned int program, const char* name)
{
	if (program == 0 || name == nullptr || name[0] == '\0')
	{
		return -1;
	}
	auto& perProgram = mUniformLocationCache[program];
	const auto it = perProgram.find(name);
	if (it != perProgram.end())
	{
		return it->second;
	}
	const int loc = glGetUniformLocation(program, name);
	perProgram.emplace(name, loc);
	return loc;
}

void GameGl::clearUniformLocationCache()
{
	mUniformLocationCache.clear();
}

void GameGl::setCameraHeight(float value)
{
	if (std::fabs(mCameraHeight - value) > 1e-6f)
	{
		mCameraHeight = value;
		mCameraPosition.z = value;
		markSkyAndApDirty();
	}
}

void GameGl::setPostTonemapMode(int mode)
{
	int clamped = mode;
	if (clamped < 0) clamped = 0;
	if (clamped > 2) clamped = 2;
	mPostTonemapMode = clamped;
}

void GameGl::setPostExposure(float exposure)
{
	const float clamped = exposure < 0.0f ? 0.0f : exposure;
	mPostExposure = clamped;
}

void GameGl::setPostOutputGamma(float gamma)
{
	const float clamped = gamma < 1.0f ? 1.0f : gamma;
	mPostOutputGamma = clamped;
}

void GameGl::setCameraForward(float value)
{
	if (std::fabs(mCameraForward - value) > 1e-6f)
	{
		mCameraForward = value;
		mCameraPosition.y = value;
		markSkyAndApDirty();
	}
}

void GameGl::setCameraOffset(const GlVec3& value)
{
	const bool changed =
		std::fabs(mCameraPosition.x - value.x) > 1e-6f ||
		std::fabs(mCameraPosition.y - value.y) > 1e-6f ||
		std::fabs(mCameraPosition.z - value.z) > 1e-6f;
	if (changed)
	{
		mCameraPosition = value;
		mCameraForward = value.y;
		mCameraHeight = value.z;
		markSkyAndApDirty();
		updateViewAndSunDirections();
	}
}

void GameGl::setViewYaw(float value)
{
	if (std::fabs(mViewYaw - value) > 1e-6f)
	{
		mViewYaw = value;
		mAerialPerspectiveDirty = true;
		updateViewAndSunDirections();
	}
}

void GameGl::setViewPitch(float value)
{
	const float clamped = value < -1.55f ? -1.55f : (value > 1.55f ? 1.55f : value);
	if (std::fabs(mViewPitch - clamped) > 1e-6f)
	{
		mViewPitch = clamped;
		mAerialPerspectiveDirty = true;
		updateViewAndSunDirections();
	}
}

void GameGl::setSunIlluminanceScale(float value)
{
	if (std::fabs(mSunIlluminanceScale - value) > 1e-6f)
	{
		mSunIlluminanceScale = value;
		markSkyAndApDirty();
	}
}

void GameGl::setSunYaw(float value)
{
	if (std::fabs(mSunYaw - value) > 1e-6f)
	{
		mSunYaw = value;
		mShadowMapDirty = true;
		markSkyAndApDirty();
	}
}

void GameGl::setSunPitch(float value)
{
	if (std::fabs(mSunPitch - value) > 1e-6f)
	{
		mSunPitch = value;
		mShadowMapDirty = true;
		markSkyAndApDirty();
	}
}

void GameGl::setRayMarchMinSpp(int value)
{
	const int clamped = value < 1 ? 1 : value;
	if (mRayMarchMinSpp != clamped)
	{
		mRayMarchMinSpp = clamped;
		if (mRayMarchMaxSpp <= mRayMarchMinSpp)
		{
			mRayMarchMaxSpp = mRayMarchMinSpp + 1;
		}
	}
}

void GameGl::setRayMarchMaxSpp(int value)
{
	int clamped = value < 2 ? 2 : value;
	if (clamped <= mRayMarchMinSpp)
	{
		clamped = mRayMarchMinSpp + 1;
	}
	if (mRayMarchMaxSpp != clamped)
	{
		mRayMarchMaxSpp = clamped;
	}
}

void GameGl::setFastSky(bool enabled)
{
	mFastSky = enabled;
}

void GameGl::setFastAerialPerspective(bool enabled)
{
	mFastAerialPerspective = enabled;
}

void GameGl::setAerialPerspectivePreviewSlice(int value)
{
	const int depth = static_cast<int>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_DEPTH);
	if (depth <= 0)
	{
		mAerialPerspectivePreviewSlice = 0;
		return;
	}
	int clamped = value;
	if (clamped < 0) clamped = 0;
	if (clamped >= depth) clamped = depth - 1;
	mAerialPerspectivePreviewSlice = clamped;
}

void GameGl::setMultipleScatteringFactor(float value)
{
	if (std::fabs(mMultipleScatteringFactor - value) > 1e-6f)
	{
		mMultipleScatteringFactor = value;
		markLutsDirty();
	}
}

void GameGl::setAtmosphereInfo(const GlAtmosphereInfo& value)
{
	mAtmosphereInfo = value;
	markLutsDirty();
}

void GameGl::updateViewAndSunDirections()
{
	const glm::vec3 viewDir = makeViewDirectionZUpForwardY(mViewYaw, mViewPitch);
	mViewDir = { viewDir.x, viewDir.y, viewDir.z };

	const glm::vec3 worldUp(0.0f, 0.0f, 1.0f);
	glm::vec3 viewRight = glm::cross(worldUp, viewDir);
	if (glm::length(viewRight) < 1e-6f)
	{
		viewRight = glm::vec3(1.0f, 0.0f, 0.0f);
	}
	else
	{
		viewRight = glm::normalize(viewRight);
	}
	glm::vec3 viewUp = glm::cross(viewDir, viewRight);
	if (glm::length(viewUp) < 1e-6f)
	{
		viewUp = worldUp;
	}
	else
	{
		viewUp = glm::normalize(viewUp);
	}
	mViewRight = { viewRight.x, viewRight.y, viewRight.z };
	mViewUp = { viewUp.x, viewUp.y, viewUp.z };

	mCameraOffset = mCameraPosition;
	mCameraForward = mCameraPosition.y;
	mCameraHeight = mCameraPosition.z;

	const glm::vec3 sunDir = makeViewDirectionZUpForwardY(mSunYaw, mSunPitch);
	mSunDir = { sunDir.x, sunDir.y, sunDir.z };
}

void GameGl::updateShadowViewProj()
{
	const glm::vec3 eyePosition(0.0f, 0.0f, 0.0f);
	const glm::vec3 focusPosition(-mSunDir.x, -mSunDir.y, -mSunDir.z);
	const glm::vec3 upDirection(0.0f, 0.0f, 1.0f);
	const glm::mat4 viewMatrix = glm::lookAtLH(eyePosition, focusPosition, upDirection);
	const glm::mat4 projMatrix = glm::orthoLH_NO(
		-100.0f,
		100.0f,
		-100.0f,
		100.0f,
		-100.0f,
		100.0f);
	const glm::mat4 viewProjMatrix = projMatrix * viewMatrix;
	std::memcpy(mShadowViewProj, glm::value_ptr(viewProjMatrix), sizeof(mShadowViewProj));
}

void GameGl::createGpuPassTimers()
{
	mGpuPassTimingsSupported = true;

	GpuPassTimer* timers[] = {
		&mShadowPassTimer,
		&mTransmittancePassTimer,
		&mMultiScatteringPassTimer,
		&mSkyViewPassTimer,
		&mAerialPerspectivePassTimer,
		&mTerrainPassTimer,
		&mPresentPassTimer
	};

	for (GpuPassTimer* timer : timers)
	{
		timer->query = 0;
		timer->lastMs = 0.0f;
		timer->pending = false;
		timer->active = false;
		glGenQueries(1, &timer->query);
	}

	const unsigned int err = glGetError();
	if (err != GL_NO_ERROR)
	{
		destroyGpuPassTimers();
		mGpuPassTimingsSupported = false;
	}
}

void GameGl::destroyGpuPassTimers()
{
	GpuPassTimer* timers[] = {
		&mShadowPassTimer,
		&mTransmittancePassTimer,
		&mMultiScatteringPassTimer,
		&mSkyViewPassTimer,
		&mAerialPerspectivePassTimer,
		&mTerrainPassTimer,
		&mPresentPassTimer
	};

	for (GpuPassTimer* timer : timers)
	{
		if (timer->query != 0)
		{
			glDeleteQueries(1, &timer->query);
			timer->query = 0;
		}
		timer->lastMs = 0.0f;
		timer->pending = false;
		timer->active = false;
	}

	mGpuPassTimingsSupported = false;
}

void GameGl::resolveGpuPassTimers()
{
	if (!mGpuPassTimingsSupported)
	{
		return;
	}

	GpuPassTimer* timers[] = {
		&mShadowPassTimer,
		&mTransmittancePassTimer,
		&mMultiScatteringPassTimer,
		&mSkyViewPassTimer,
		&mAerialPerspectivePassTimer,
		&mTerrainPassTimer,
		&mPresentPassTimer
	};

	for (GpuPassTimer* timer : timers)
	{
		if (timer->query == 0 || !timer->pending)
		{
			continue;
		}
		unsigned int available = 0;
		glGetQueryObjectuiv(timer->query, GL_QUERY_RESULT_AVAILABLE, &available);
		if (available == 0)
		{
			continue;
		}
		GLuint64 elapsedNs = 0;
		glGetQueryObjectui64v(timer->query, GL_QUERY_RESULT, &elapsedNs);
		timer->lastMs = static_cast<float>(elapsedNs * (1.0 / 1000000.0));
		timer->pending = false;
	}
}

void GameGl::beginGpuPassTimer(GpuPassTimer& timer)
{
	if (!mGpuPassTimingsSupported || timer.query == 0 || timer.pending || timer.active)
	{
		return;
	}
	glBeginQuery(GL_TIME_ELAPSED, timer.query);
	timer.active = true;
}

void GameGl::endGpuPassTimer(GpuPassTimer& timer)
{
	if (!mGpuPassTimingsSupported || !timer.active)
	{
		return;
	}
	glEndQuery(GL_TIME_ELAPSED);
	timer.active = false;
	timer.pending = true;
}

unsigned int GameGl::loadAndCompileShader(unsigned int type, const char* path)
{
	const std::string source = readTextFile(path);
	if (source.empty())
	{
		showMessageBox("OpenGL shader error", std::string("Failed to read shader file: ") + path);
		return 0;
	}

	const char* srcPtr = source.c_str();
	unsigned int shader = glCreateShader(type);
	glShaderSource(shader, 1, &srcPtr, nullptr);
	glCompileShader(shader);

	int success = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (success == GL_TRUE)
	{
		return shader;
	}

	int logLength = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
	std::string log;
	log.resize(logLength > 1 ? size_t(logLength) : 1);
	glGetShaderInfoLog(shader, logLength, nullptr, &log[0]);
	showMessageBox("OpenGL shader compile error", std::string("File: ") + path + "\n\n" + log);
	glDeleteShader(shader);
	return 0;
}

unsigned int GameGl::linkProgram(unsigned int vs, unsigned int fs, const char* debugName)
{
	unsigned int program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);

	int success = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (success == GL_TRUE)
	{
		return program;
	}

	int logLength = 0;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
	std::string log;
	log.resize(logLength > 1 ? size_t(logLength) : 1);
	glGetProgramInfoLog(program, logLength, nullptr, &log[0]);
	showMessageBox("OpenGL program link error", std::string("Program: ") + debugName + "\n\n" + log);
	glDeleteProgram(program);
	return 0;
}

unsigned int GameGl::linkComputeProgram(unsigned int cs, const char* debugName)
{
	unsigned int program = glCreateProgram();
	glAttachShader(program, cs);
	glLinkProgram(program);

	int success = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (success == GL_TRUE)
	{
		return program;
	}

	int logLength = 0;
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
	std::string log;
	log.resize(logLength > 1 ? size_t(logLength) : 1);
	glGetProgramInfoLog(program, logLength, nullptr, &log[0]);
	showMessageBox("OpenGL program link error", std::string("Program: ") + debugName + "\n\n" + log);
	glDeleteProgram(program);
	return 0;
}

bool GameGl::createPrograms()
{
	const unsigned int fullscreenVs = loadAndCompileShader(GL_VERTEX_SHADER, kFullscreenVertexShaderPath);
	const unsigned int transmittanceFs = loadAndCompileShader(GL_FRAGMENT_SHADER, kTransmittanceFragmentShaderPath);
	const unsigned int multiScatteringCs = loadAndCompileShader(GL_COMPUTE_SHADER, kMultiScatteringComputeShaderPath);
	const unsigned int skyViewFs = loadAndCompileShader(GL_FRAGMENT_SHADER, kSkyViewFragmentShaderPath);
	const unsigned int aerialPerspectiveCs = loadAndCompileShader(GL_COMPUTE_SHADER, kAerialPerspectiveComputeShaderPath);
	const unsigned int terrainVs = loadAndCompileShader(GL_VERTEX_SHADER, kTerrainVertexShaderPath);
	const unsigned int terrainFs = loadAndCompileShader(GL_FRAGMENT_SHADER, kTerrainFragmentShaderPath);
	const unsigned int terrainShadowFs = loadAndCompileShader(GL_FRAGMENT_SHADER, kTerrainShadowFragmentShaderPath);
	const unsigned int raymarchFs = loadAndCompileShader(GL_FRAGMENT_SHADER, kRaymarchFragmentShaderPath);
	const unsigned int postProcessFs = loadAndCompileShader(GL_FRAGMENT_SHADER, kPostProcessFragmentShaderPath);

	if (fullscreenVs == 0 || transmittanceFs == 0 || multiScatteringCs == 0 || skyViewFs == 0 || aerialPerspectiveCs == 0 || terrainVs == 0 || terrainFs == 0 || terrainShadowFs == 0 || raymarchFs == 0 || postProcessFs == 0)
	{
		if (fullscreenVs != 0) glDeleteShader(fullscreenVs);
		if (transmittanceFs != 0) glDeleteShader(transmittanceFs);
		if (multiScatteringCs != 0) glDeleteShader(multiScatteringCs);
		if (skyViewFs != 0) glDeleteShader(skyViewFs);
		if (aerialPerspectiveCs != 0) glDeleteShader(aerialPerspectiveCs);
		if (terrainVs != 0) glDeleteShader(terrainVs);
		if (terrainFs != 0) glDeleteShader(terrainFs);
		if (terrainShadowFs != 0) glDeleteShader(terrainShadowFs);
		if (raymarchFs != 0) glDeleteShader(raymarchFs);
		if (postProcessFs != 0) glDeleteShader(postProcessFs);
		return false;
	}

	mTransmittanceProgram = linkProgram(fullscreenVs, transmittanceFs, "TransmittanceLut");
	mMultiScatteringProgram = linkComputeProgram(multiScatteringCs, "NewMultiScatteringLut");
	mSkyViewProgram = linkProgram(fullscreenVs, skyViewFs, "SkyViewLut");
	mAerialPerspectiveProgram = linkComputeProgram(aerialPerspectiveCs, "AerialPerspectiveVolume");
	mTerrainProgram = linkProgram(terrainVs, terrainFs, "Terrain");
	mTerrainShadowProgram = linkProgram(terrainVs, terrainShadowFs, "TerrainShadow");
	mRaymarchProgram = linkProgram(fullscreenVs, raymarchFs, "RenderRaymarchingHillaire");
	mPostProcessProgram = linkProgram(fullscreenVs, postProcessFs, "PostProcess");

	glDeleteShader(fullscreenVs);
	glDeleteShader(transmittanceFs);
	glDeleteShader(multiScatteringCs);
	glDeleteShader(skyViewFs);
	glDeleteShader(aerialPerspectiveCs);
	glDeleteShader(terrainVs);
	glDeleteShader(terrainFs);
	glDeleteShader(terrainShadowFs);
	glDeleteShader(raymarchFs);
	glDeleteShader(postProcessFs);

	if (mTransmittanceProgram == 0 || mMultiScatteringProgram == 0 || mSkyViewProgram == 0 || mAerialPerspectiveProgram == 0 || mTerrainProgram == 0 || mTerrainShadowProgram == 0 || mRaymarchProgram == 0 || mPostProcessProgram == 0)
	{
		destroyPrograms();
		return false;
	}

	return true;
}

bool GameGl::createTransmittanceResources()
{
	glGenTextures(1, &mTransmittanceTex);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGBA16F,
		static_cast<GLsizei>(mLutsInfo.TRANSMITTANCE_TEXTURE_WIDTH),
		static_cast<GLsizei>(mLutsInfo.TRANSMITTANCE_TEXTURE_HEIGHT),
		0,
		GL_RGBA,
		GL_FLOAT,
		nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &mTransmittanceFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, mTransmittanceFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTransmittanceTex, 0);
	const unsigned int drawBuffer = GL_COLOR_ATTACHMENT0;
	glDrawBuffers(1, &drawBuffer);

	const unsigned int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		showMessageBox("OpenGL framebuffer error", "Failed to create transmittance LUT framebuffer.");
		destroyTransmittanceResources();
		return false;
	}
	return true;
}

bool GameGl::createMultipleScatteringResources()
{
	const GLsizei lutSize = static_cast<GLsizei>(mLutsInfo.MULTI_SCATTERING_TEXTURE_SIZE);

	glGenTextures(1, &mMultiScatteringTex);
	glBindTexture(GL_TEXTURE_2D, mMultiScatteringTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGBA32F,
		lutSize,
		lutSize,
		0,
		GL_RGBA,
		GL_FLOAT,
		nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenTextures(1, &mMultiScatteringPreviewTex);
	glBindTexture(GL_TEXTURE_2D, mMultiScatteringPreviewTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, lutSize, lutSize, 0, GL_RGBA, GL_FLOAT, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);
	return true;
}

bool GameGl::createSkyViewResources()
{
	const int skyWidth = static_cast<int>(mLutsInfo.SKY_VIEW_TEXTURE_WIDTH);
	const int skyHeight = static_cast<int>(mLutsInfo.SKY_VIEW_TEXTURE_HEIGHT);

	glGenTextures(1, &mSkyViewTex);
	glBindTexture(GL_TEXTURE_2D, mSkyViewTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, skyWidth, skyHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &mSkyViewFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, mSkyViewFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mSkyViewTex, 0);
	const unsigned int drawBuffer = GL_COLOR_ATTACHMENT0;
	glDrawBuffers(1, &drawBuffer);

	const unsigned int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		showMessageBox("OpenGL framebuffer error", "Failed to create sky-view LUT framebuffer.");
		destroySkyViewResources();
		return false;
	}
	return true;
}

bool GameGl::createAerialPerspectiveResources()
{
	const GLsizei apWidth = static_cast<GLsizei>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_WIDTH);
	const GLsizei apHeight = static_cast<GLsizei>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_HEIGHT);
	const GLsizei apDepth = static_cast<GLsizei>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_DEPTH);

	glGenTextures(1, &mAerialPerspectiveTex);
	glBindTexture(GL_TEXTURE_3D, mAerialPerspectiveTex);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexImage3D(
		GL_TEXTURE_3D,
		0,
		GL_RGBA16F,
		apWidth,
		apHeight,
		apDepth,
		0,
		GL_RGBA,
		GL_FLOAT,
		nullptr);
	glBindTexture(GL_TEXTURE_3D, 0);

	glGenTextures(1, &mAerialPerspectivePreviewTex);
	glBindTexture(GL_TEXTURE_2D, mAerialPerspectivePreviewTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, apWidth, apHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	mAerialPerspectivePreviewSlice = static_cast<int>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_DEPTH / 2u);
	return true;
}

bool GameGl::createTerrainResources()
{
	int width = 0;
	int height = 0;
	std::vector<float> pixels;
	std::string exrError;
	const bool loaded = loadExrRgba("Resources/heightmap1.exr", width, height, pixels, exrError);

	glGenTextures(1, &mTerrainHeightmapTex);
	glBindTexture(GL_TEXTURE_2D, mTerrainHeightmapTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (loaded)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, pixels.data());
	}
	else
	{
		const float fallbackPixel[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, fallbackPixel);
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	if (!loaded)
	{
		std::fprintf(stderr, "OpenGL terrain warning: failed to load heightmap1.exr. Using flat fallback terrain. %s\n", exrError.c_str());
	}
	return true;
}

bool GameGl::createShadowResources()
{
	const GLsizei shadowSize = static_cast<GLsizei>(mShadowMapSize);

	glGenTextures(1, &mShadowDepthTex);
	glBindTexture(GL_TEXTURE_2D, mShadowDepthTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	const float borderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, shadowSize, shadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	// 1x1 always-lit shadow texture for fast runtime toggling without shader permutations.
	glGenTextures(1, &mShadowFallbackTex);
	glBindTexture(GL_TEXTURE_2D, mShadowFallbackTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	const float litDepth = 1.0f;
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &litDepth);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &mShadowFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, mShadowFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mShadowDepthTex, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	const unsigned int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		showMessageBox("OpenGL framebuffer error", "Failed to create shadow framebuffer.");
		destroyShadowResources();
		return false;
	}
	return true;
}

bool GameGl::createSceneResources()
{
	const GLsizei width = static_cast<GLsizei>(mBackbufferWidth);
	const GLsizei height = static_cast<GLsizei>(mBackbufferHeight);

	glGenTextures(1, &mSceneHdrTex);
	glBindTexture(GL_TEXTURE_2D, mSceneHdrTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenTextures(1, &mSceneLinearDepthTex);
	glBindTexture(GL_TEXTURE_2D, mSceneLinearDepthTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenTextures(1, &mSceneDepthTex);
	glBindTexture(GL_TEXTURE_2D, mSceneDepthTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &mSceneFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, mSceneFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mSceneHdrTex, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mSceneLinearDepthTex, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, mSceneDepthTex, 0);
	const unsigned int drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(2, drawBuffers);
	const unsigned int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		showMessageBox("OpenGL framebuffer error", "Failed to create scene framebuffer.");
		destroySceneResources();
		return false;
	}

	glGenTextures(1, &mFinalHdrTex);
	glBindTexture(GL_TEXTURE_2D, mFinalHdrTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &mFinalHdrFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, mFinalHdrFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mFinalHdrTex, 0);
	const unsigned int finalDrawBuffer = GL_COLOR_ATTACHMENT0;
	glDrawBuffers(1, &finalDrawBuffer);
	const unsigned int finalStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (finalStatus != GL_FRAMEBUFFER_COMPLETE)
	{
		showMessageBox("OpenGL framebuffer error", "Failed to create final HDR framebuffer.");
		destroySceneResources();
		return false;
	}
	return true;
}

void GameGl::destroyPrograms()
{
	clearUniformLocationCache();
	if (mPostProcessProgram != 0)
	{
		glDeleteProgram(mPostProcessProgram);
		mPostProcessProgram = 0;
	}
	if (mRaymarchProgram != 0)
	{
		glDeleteProgram(mRaymarchProgram);
		mRaymarchProgram = 0;
	}
	if (mSkyViewProgram != 0)
	{
		glDeleteProgram(mSkyViewProgram);
		mSkyViewProgram = 0;
	}
	if (mAerialPerspectiveProgram != 0)
	{
		glDeleteProgram(mAerialPerspectiveProgram);
		mAerialPerspectiveProgram = 0;
	}
	if (mTerrainProgram != 0)
	{
		glDeleteProgram(mTerrainProgram);
		mTerrainProgram = 0;
	}
	if (mTerrainShadowProgram != 0)
	{
		glDeleteProgram(mTerrainShadowProgram);
		mTerrainShadowProgram = 0;
	}
	if (mMultiScatteringProgram != 0)
	{
		glDeleteProgram(mMultiScatteringProgram);
		mMultiScatteringProgram = 0;
	}
	if (mTransmittanceProgram != 0)
	{
		glDeleteProgram(mTransmittanceProgram);
		mTransmittanceProgram = 0;
	}
}

void GameGl::destroyTransmittanceResources()
{
	if (mTransmittanceFbo != 0)
	{
		glDeleteFramebuffers(1, &mTransmittanceFbo);
		mTransmittanceFbo = 0;
	}
	if (mTransmittanceTex != 0)
	{
		glDeleteTextures(1, &mTransmittanceTex);
		mTransmittanceTex = 0;
	}
}

void GameGl::destroyMultipleScatteringResources()
{
	if (mMultiScatteringPreviewTex != 0)
	{
		glDeleteTextures(1, &mMultiScatteringPreviewTex);
		mMultiScatteringPreviewTex = 0;
	}
	if (mMultiScatteringTex != 0)
	{
		glDeleteTextures(1, &mMultiScatteringTex);
		mMultiScatteringTex = 0;
	}
}

void GameGl::destroySkyViewResources()
{
	if (mSkyViewFbo != 0)
	{
		glDeleteFramebuffers(1, &mSkyViewFbo);
		mSkyViewFbo = 0;
	}
	if (mSkyViewTex != 0)
	{
		glDeleteTextures(1, &mSkyViewTex);
		mSkyViewTex = 0;
	}
}

void GameGl::destroyAerialPerspectiveResources()
{
	if (mAerialPerspectivePreviewTex != 0)
	{
		glDeleteTextures(1, &mAerialPerspectivePreviewTex);
		mAerialPerspectivePreviewTex = 0;
	}
	if (mAerialPerspectiveTex != 0)
	{
		glDeleteTextures(1, &mAerialPerspectiveTex);
		mAerialPerspectiveTex = 0;
	}
}

void GameGl::destroyTerrainResources()
{
	if (mTerrainHeightmapTex != 0)
	{
		glDeleteTextures(1, &mTerrainHeightmapTex);
		mTerrainHeightmapTex = 0;
	}
}

void GameGl::destroyShadowResources()
{
	if (mShadowFbo != 0)
	{
		glDeleteFramebuffers(1, &mShadowFbo);
		mShadowFbo = 0;
	}
	if (mShadowDepthTex != 0)
	{
		glDeleteTextures(1, &mShadowDepthTex);
		mShadowDepthTex = 0;
	}
	if (mShadowFallbackTex != 0)
	{
		glDeleteTextures(1, &mShadowFallbackTex);
		mShadowFallbackTex = 0;
	}
}

void GameGl::destroySceneResources()
{
	if (mFinalHdrFbo != 0)
	{
		glDeleteFramebuffers(1, &mFinalHdrFbo);
		mFinalHdrFbo = 0;
	}
	if (mFinalHdrTex != 0)
	{
		glDeleteTextures(1, &mFinalHdrTex);
		mFinalHdrTex = 0;
	}
	if (mSceneFbo != 0)
	{
		glDeleteFramebuffers(1, &mSceneFbo);
		mSceneFbo = 0;
	}
	if (mSceneDepthTex != 0)
	{
		glDeleteTextures(1, &mSceneDepthTex);
		mSceneDepthTex = 0;
	}
	if (mSceneLinearDepthTex != 0)
	{
		glDeleteTextures(1, &mSceneLinearDepthTex);
		mSceneLinearDepthTex = 0;
	}
	if (mSceneHdrTex != 0)
	{
		glDeleteTextures(1, &mSceneHdrTex);
		mSceneHdrTex = 0;
	}
}

bool GameGl::initialise()
{
	setupEarthAtmosphere(mAtmosphereInfo);
	updateViewAndSunDirections();
	createGpuPassTimers();
	glGenVertexArrays(1, &mFullscreenVao);

	if (!createPrograms())
	{
		shutdown();
		return false;
	}
	if (!createTransmittanceResources() || !createMultipleScatteringResources() || !createSkyViewResources() || !createAerialPerspectiveResources() || !createTerrainResources() || !createShadowResources() || !createSceneResources())
	{
		shutdown();
		return false;
	}

	mLutDirty = true;
	mSkyViewDirty = true;
	mAerialPerspectiveDirty = true;
	mInitialised = true;
	return true;
}

void GameGl::shutdown()
{
	destroyGpuPassTimers();
	destroySceneResources();
	destroyShadowResources();
	destroyTerrainResources();
	destroyAerialPerspectiveResources();
	destroySkyViewResources();
	destroyMultipleScatteringResources();
	destroyTransmittanceResources();
	destroyPrograms();
	if (mFullscreenVao != 0)
	{
		glDeleteVertexArrays(1, &mFullscreenVao);
		mFullscreenVao = 0;
	}
	mInitialised = false;
}

void GameGl::resize(int width, int height)
{
	const int newWidth = width > 1 ? width : 1;
	const int newHeight = height > 1 ? height : 1;
	if (newWidth != mBackbufferWidth || newHeight != mBackbufferHeight)
	{
		mBackbufferWidth = newWidth;
		mBackbufferHeight = newHeight;
		destroySceneResources();
		if (!createSceneResources())
		{
			mInitialised = false;
			return;
		}
		mAerialPerspectiveDirty = true;
	}
}

void GameGl::uploadAtmosphereUniforms(unsigned int program)
{
	const GlVec3 atmosphereCameraOffset = mCameraOffset;
	const float atmosphereCameraHeight = mCameraOffset.z;

	const auto set1f = [&](const char* name, float value)
	{
		const int loc = getUniformLocationCached(program, name);
		if (loc >= 0) glUniform1f(loc, value);
	};
	const auto set1i = [&](const char* name, int value)
	{
		const int loc = getUniformLocationCached(program, name);
		if (loc >= 0) glUniform1i(loc, value);
	};
	const auto set3f = [&](const char* name, const GlVec3& v)
	{
		const int loc = getUniformLocationCached(program, name);
		if (loc >= 0) glUniform3f(loc, v.x, v.y, v.z);
	};

	set1f("u_bottom_radius", mAtmosphereInfo.bottom_radius);
	set1f("u_top_radius", mAtmosphereInfo.top_radius);
	set1f("u_mie_phase_g", mAtmosphereInfo.mie_phase_g);
	set1f("u_rayleigh_density_exp_scale", mAtmosphereInfo.rayleigh_density_exp_scale);
	set1f("u_mie_density_exp_scale", mAtmosphereInfo.mie_density_exp_scale);
	set1f("u_absorption_layer0_width", mAtmosphereInfo.absorption_layer0_width);
	set1f("u_absorption_layer0_linear_term", mAtmosphereInfo.absorption_layer0_linear_term);
	set1f("u_absorption_layer0_constant_term", mAtmosphereInfo.absorption_layer0_constant_term);
	set1f("u_absorption_layer1_linear_term", mAtmosphereInfo.absorption_layer1_linear_term);
	set1f("u_absorption_layer1_constant_term", mAtmosphereInfo.absorption_layer1_constant_term);
	set1f("u_camera_height", atmosphereCameraHeight);
	set1f("u_raymarch_spp_min", static_cast<float>(mRayMarchMinSpp));
	set1f("u_raymarch_spp_max", static_cast<float>(mRayMarchMaxSpp));
	set1f("u_sun_illuminance", mSunIlluminanceScale);
	set1f("u_multiple_scattering_factor", mMultipleScatteringFactor);
	set3f("u_rayleigh_scattering", mAtmosphereInfo.rayleigh_scattering);
	set3f("u_mie_scattering", mAtmosphereInfo.mie_scattering);
	set3f("u_mie_extinction", mAtmosphereInfo.mie_extinction);
	set3f("u_mie_absorption", mAtmosphereInfo.mie_absorption);
	set3f("u_absorption_extinction", mAtmosphereInfo.absorption_extinction);
	set3f("u_ground_albedo", mAtmosphereInfo.ground_albedo);
	set3f("u_sun_direction", mSunDir);
	set3f("u_camera_offset", atmosphereCameraOffset);
	set3f("u_view_forward", mViewDir);
	set3f("u_view_right", mViewRight);
	set3f("u_view_up", mViewUp);

	set1i("u_transmittance_width", static_cast<int>(mLutsInfo.TRANSMITTANCE_TEXTURE_WIDTH));
	set1i("u_transmittance_height", static_cast<int>(mLutsInfo.TRANSMITTANCE_TEXTURE_HEIGHT));
	set1i("u_multi_scattering_lut_res", static_cast<int>(mLutsInfo.MULTI_SCATTERING_TEXTURE_SIZE));
	set1i("u_skyview_width", static_cast<int>(mLutsInfo.SKY_VIEW_TEXTURE_WIDTH));
	set1i("u_skyview_height", static_cast<int>(mLutsInfo.SKY_VIEW_TEXTURE_HEIGHT));
	set1i("u_ap_width", static_cast<int>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_WIDTH));
	set1i("u_ap_height", static_cast<int>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_HEIGHT));
	set1i("u_ap_depth", static_cast<int>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_DEPTH));
	set1i("u_fast_sky", mFastSky ? 1 : 0);
	set1i("u_fast_aerial_perspective", mFastAerialPerspective ? 1 : 0);
	set1i("u_colored_transmittance", (!mFastAerialPerspective && mColoredTransmittance) ? 1 : 0);
}

void GameGl::renderTransmittanceLut()
{
	beginGpuPassTimer(mTransmittancePassTimer);
	glBindFramebuffer(GL_FRAMEBUFFER, mTransmittanceFbo);
	glViewport(0, 0, static_cast<GLsizei>(mLutsInfo.TRANSMITTANCE_TEXTURE_WIDTH), static_cast<GLsizei>(mLutsInfo.TRANSMITTANCE_TEXTURE_HEIGHT));
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	glUseProgram(mTransmittanceProgram);
	uploadAtmosphereUniforms(mTransmittanceProgram);
	glBindVertexArray(mFullscreenVao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindVertexArray(0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	endGpuPassTimer(mTransmittancePassTimer);
}

void GameGl::renderMultipleScatteringLut()
{
	beginGpuPassTimer(mMultiScatteringPassTimer);
	glUseProgram(mMultiScatteringProgram);
	uploadAtmosphereUniforms(mMultiScatteringProgram);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	const int transLoc = getUniformLocationCached(mMultiScatteringProgram, "u_transmittance_lut");
	if (transLoc >= 0) glUniform1i(transLoc, 0);

	glBindImageTexture(0, mMultiScatteringTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
	glDispatchCompute(
		static_cast<GLuint>(mLutsInfo.MULTI_SCATTERING_TEXTURE_SIZE),
		static_cast<GLuint>(mLutsInfo.MULTI_SCATTERING_TEXTURE_SIZE),
		1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
	const unsigned int computeError = glGetError();
	if (computeError != GL_NO_ERROR)
	{
		char errorBuffer[128] = {};
		std::snprintf(errorBuffer, sizeof(errorBuffer), "glDispatchCompute failed with GL error 0x%X.", computeError);
		showMessageBox("OpenGL compute error", errorBuffer);
	}

	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	endGpuPassTimer(mMultiScatteringPassTimer);

	if (mLutPreviewUpdatesEnabled)
	{
		updateMultiScatteringDebugStats();
	}
}

void GameGl::updateMultiScatteringDebugStats()
{
	const size_t size = static_cast<size_t>(mLutsInfo.MULTI_SCATTERING_TEXTURE_SIZE);
	const size_t pixelCount = size * size;
	std::vector<float> pixels(pixelCount * 4, 0.0f);

	glBindTexture(GL_TEXTURE_2D, mMultiScatteringTex);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	const unsigned int err = glGetError();
	if (err != GL_NO_ERROR)
	{
		mMultiScatteringStatsValid = false;
		return;
	}

	float minV = std::numeric_limits<float>::infinity();
	float maxV = -std::numeric_limits<float>::infinity();
	for (size_t i = 0; i < pixelCount; ++i)
	{
		const size_t base = i * 4;
		const float r = pixels[base + 0];
		const float g = pixels[base + 1];
		const float b = pixels[base + 2];
		minV = (r < minV) ? r : minV;
		minV = (g < minV) ? g : minV;
		minV = (b < minV) ? b : minV;
		maxV = (r > maxV) ? r : maxV;
		maxV = (g > maxV) ? g : maxV;
		maxV = (b > maxV) ? b : maxV;
	}

	mMultiScatteringDebugMin = minV;
	mMultiScatteringDebugMax = maxV;
	mMultiScatteringStatsValid = true;
}

void GameGl::renderAerialPerspectiveVolume()
{
	beginGpuPassTimer(mAerialPerspectivePassTimer);
	glUseProgram(mAerialPerspectiveProgram);
	uploadAtmosphereUniforms(mAerialPerspectiveProgram);
	const int aspectLoc = getUniformLocationCached(mAerialPerspectiveProgram, "u_aspect");
	if (aspectLoc >= 0) glUniform1f(aspectLoc, static_cast<float>(mBackbufferWidth) / static_cast<float>(mBackbufferHeight));
	const int fovLoc = getUniformLocationCached(mAerialPerspectiveProgram, "u_fov_y_degrees");
	if (fovLoc >= 0) glUniform1f(fovLoc, kMainCameraFovYDegrees);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	const int transLoc = getUniformLocationCached(mAerialPerspectiveProgram, "u_transmittance_lut");
	if (transLoc >= 0) glUniform1i(transLoc, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mMultiScatteringTex);
	const int multiLoc = getUniformLocationCached(mAerialPerspectiveProgram, "u_multiscattering_lut");
	if (multiLoc >= 0) glUniform1i(multiLoc, 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, mShadowMapsEnabled ? mShadowDepthTex : mShadowFallbackTex);
	const int shadowTexLoc = getUniformLocationCached(mAerialPerspectiveProgram, "u_shadowmap_tex");
	if (shadowTexLoc >= 0) glUniform1i(shadowTexLoc, 2);
	const int shadowViewProjLoc = getUniformLocationCached(mAerialPerspectiveProgram, "u_shadow_view_proj");
	if (shadowViewProjLoc >= 0) glUniformMatrix4fv(shadowViewProjLoc, 1, GL_FALSE, mShadowViewProj);

	glBindImageTexture(0, mAerialPerspectiveTex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	const GLuint groupX = (mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_WIDTH + 7u) / 8u;
	const GLuint groupY = (mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_HEIGHT + 7u) / 8u;
	const GLuint groupZ = mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_DEPTH;
	glDispatchCompute(groupX, groupY, groupZ);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

	const unsigned int computeError = glGetError();
	if (computeError != GL_NO_ERROR)
	{
		char errorBuffer[128] = {};
		std::snprintf(errorBuffer, sizeof(errorBuffer), "Aerial perspective compute failed with GL error 0x%X.", computeError);
		showMessageBox("OpenGL compute error", errorBuffer);
	}

	glBindImageTexture(0, 0, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	endGpuPassTimer(mAerialPerspectivePassTimer);

	if (mLutPreviewUpdatesEnabled)
	{
		copyAerialPerspectivePreviewSlice();
		updateAerialPerspectiveDebugStats();
	}
}

void GameGl::copyAerialPerspectivePreviewSlice()
{
	if (mAerialPerspectiveTex == 0 || mAerialPerspectivePreviewTex == 0)
	{
		return;
	}

	const int depth = static_cast<int>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_DEPTH);
	if (depth <= 0)
	{
		return;
	}
	int slice = mAerialPerspectivePreviewSlice;
	if (slice < 0) slice = 0;
	if (slice >= depth) slice = depth - 1;
	mAerialPerspectivePreviewSlice = slice;

	glCopyImageSubData(
		mAerialPerspectiveTex,
		GL_TEXTURE_3D,
		0,
		0,
		0,
		slice,
		mAerialPerspectivePreviewTex,
		GL_TEXTURE_2D,
		0,
		0,
		0,
		0,
		static_cast<GLsizei>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_WIDTH),
		static_cast<GLsizei>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_HEIGHT),
		1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
}

void GameGl::updateLutPreviewTextures()
{
	if (mMultiScatteringTex != 0 && mMultiScatteringPreviewTex != 0)
	{
		const size_t size = static_cast<size_t>(mLutsInfo.MULTI_SCATTERING_TEXTURE_SIZE);
		std::vector<float> pixels(size * size * 4u, 0.0f);
		glBindTexture(GL_TEXTURE_2D, mMultiScatteringTex);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
		glBindTexture(GL_TEXTURE_2D, 0);
		const unsigned int readErr = glGetError();
		if (readErr == GL_NO_ERROR)
		{
			for (size_t i = 0; i < size * size; ++i)
			{
				const size_t base = i * 4u;
				pixels[base + 0] = toneMapPreviewValue(pixels[base + 0], mMultiScatteringPreviewExposure);
				pixels[base + 1] = toneMapPreviewValue(pixels[base + 1], mMultiScatteringPreviewExposure);
				pixels[base + 2] = toneMapPreviewValue(pixels[base + 2], mMultiScatteringPreviewExposure);
				pixels[base + 3] = 1.0f;
			}
			glBindTexture(GL_TEXTURE_2D, mMultiScatteringPreviewTex);
			glTexSubImage2D(
				GL_TEXTURE_2D,
				0,
				0,
				0,
				static_cast<GLsizei>(size),
				static_cast<GLsizei>(size),
				GL_RGBA,
				GL_FLOAT,
				pixels.data());
			glBindTexture(GL_TEXTURE_2D, 0);
		}
	}

	if (mAerialPerspectivePreviewTex != 0)
	{
		const size_t width = static_cast<size_t>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_WIDTH);
		const size_t height = static_cast<size_t>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_HEIGHT);
		std::vector<float> pixels(width * height * 4u, 0.0f);
		glBindTexture(GL_TEXTURE_2D, mAerialPerspectivePreviewTex);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
		const unsigned int readErr = glGetError();
		if (readErr == GL_NO_ERROR)
		{
			for (size_t i = 0; i < width * height; ++i)
			{
				const size_t base = i * 4u;
				pixels[base + 0] = toneMapPreviewValue(pixels[base + 0], mAerialPerspectivePreviewExposure);
				pixels[base + 1] = toneMapPreviewValue(pixels[base + 1], mAerialPerspectivePreviewExposure);
				pixels[base + 2] = toneMapPreviewValue(pixels[base + 2], mAerialPerspectivePreviewExposure);
				pixels[base + 3] = 1.0f;
			}
			glTexSubImage2D(
				GL_TEXTURE_2D,
				0,
				0,
				0,
				static_cast<GLsizei>(width),
				static_cast<GLsizei>(height),
				GL_RGBA,
				GL_FLOAT,
				pixels.data());
		}
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void GameGl::updateAerialPerspectiveDebugStats()
{
	const size_t width = static_cast<size_t>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_WIDTH);
	const size_t height = static_cast<size_t>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_HEIGHT);
	const size_t depth = static_cast<size_t>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_DEPTH);
	const size_t pixelCount = width * height * depth;
	std::vector<float> pixels(pixelCount * 4, 0.0f);

	glBindTexture(GL_TEXTURE_3D, mAerialPerspectiveTex);
	glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, pixels.data());
	glBindTexture(GL_TEXTURE_3D, 0);

	const unsigned int err = glGetError();
	if (err != GL_NO_ERROR)
	{
		mAerialPerspectiveStatsValid = false;
		return;
	}

	float minV = std::numeric_limits<float>::infinity();
	float maxV = -std::numeric_limits<float>::infinity();
	for (size_t i = 0; i < pixelCount; ++i)
	{
		const size_t base = i * 4;
		const float r = pixels[base + 0];
		const float g = pixels[base + 1];
		const float b = pixels[base + 2];
		minV = (r < minV) ? r : minV;
		minV = (g < minV) ? g : minV;
		minV = (b < minV) ? b : minV;
		maxV = (r > maxV) ? r : maxV;
		maxV = (g > maxV) ? g : maxV;
		maxV = (b > maxV) ? b : maxV;
	}

	mAerialPerspectiveDebugMin = minV;
	mAerialPerspectiveDebugMax = maxV;
	mAerialPerspectiveStatsValid = true;
}

void GameGl::renderSkyViewLut()
{
	beginGpuPassTimer(mSkyViewPassTimer);
	glBindFramebuffer(GL_FRAMEBUFFER, mSkyViewFbo);
	glViewport(0, 0, static_cast<GLsizei>(mLutsInfo.SKY_VIEW_TEXTURE_WIDTH), static_cast<GLsizei>(mLutsInfo.SKY_VIEW_TEXTURE_HEIGHT));
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	glUseProgram(mSkyViewProgram);
	uploadAtmosphereUniforms(mSkyViewProgram);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	const int transLoc = getUniformLocationCached(mSkyViewProgram, "u_transmittance_lut");
	if (transLoc >= 0) glUniform1i(transLoc, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mMultiScatteringTex);
	const int multiLoc = getUniformLocationCached(mSkyViewProgram, "u_multiscattering_lut");
	if (multiLoc >= 0) glUniform1i(multiLoc, 1);
	glBindVertexArray(mFullscreenVao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	endGpuPassTimer(mSkyViewPassTimer);
}

void GameGl::renderShadowMap()
{
	if (!mShadowMapsEnabled || !mShadowMapDirty)
	{
		return;
	}
	beginGpuPassTimer(mShadowPassTimer);

	updateShadowViewProj();

	glBindFramebuffer(GL_FRAMEBUFFER, mShadowFbo);
	glViewport(0, 0, static_cast<GLsizei>(mShadowMapSize), static_cast<GLsizei>(mShadowMapSize));
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	const float clearDepth = 1.0f;
	glClearBufferfv(GL_DEPTH, 0, &clearDepth);

	if (!mRenderTerrain)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		endGpuPassTimer(mShadowPassTimer);
		mShadowMapDirty = false;
		return;
	}

	glUseProgram(mTerrainShadowProgram);
	const int terrainResLoc = getUniformLocationCached(mTerrainShadowProgram, "u_terrain_resolution");
	if (terrainResLoc >= 0) glUniform1i(terrainResLoc, 512);
	const int viewProjLoc = getUniformLocationCached(mTerrainShadowProgram, "u_view_proj");
	if (viewProjLoc >= 0) glUniformMatrix4fv(viewProjLoc, 1, GL_FALSE, mShadowViewProj);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTerrainHeightmapTex);
	const int heightmapLoc = getUniformLocationCached(mTerrainShadowProgram, "u_heightmap_tex");
	if (heightmapLoc >= 0) glUniform1i(heightmapLoc, 0);

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(5.0f, 5.0f);
	glBindVertexArray(mFullscreenVao);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 512 * 512);

	glBindVertexArray(0);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	endGpuPassTimer(mShadowPassTimer);
	mShadowMapDirty = false;
}

void GameGl::renderTerrainScene()
{
	beginGpuPassTimer(mTerrainPassTimer);
	glBindFramebuffer(GL_FRAMEBUFFER, mSceneFbo);
	glViewport(0, 0, mBackbufferWidth, mBackbufferHeight);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);

	const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	const float clearLinearDepth[4] = { -1.0f, 0.0f, 0.0f, 1.0f };
	const float clearDepth = 1.0f;
	glClearBufferfv(GL_COLOR, 0, clearColor);
	glClearBufferfv(GL_COLOR, 1, clearLinearDepth);
	glClearBufferfv(GL_DEPTH, 0, &clearDepth);

	if (!mRenderTerrain)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		endGpuPassTimer(mTerrainPassTimer);
		return;
	}

	const GlVec3 cameraWorld = {
		mCameraOffset.x,
		mCameraOffset.y,
		mCameraOffset.z
	};
	const glm::vec3 eyePosition(cameraWorld.x, cameraWorld.y, cameraWorld.z);
	const glm::vec3 focusPosition(cameraWorld.x + mViewDir.x, cameraWorld.y + mViewDir.y, cameraWorld.z + mViewDir.z);
	const glm::vec3 upDirection(0.0f, 0.0f, 1.0f);
	const glm::mat4 viewMatrix = glm::lookAtLH(eyePosition, focusPosition, upDirection);
	const float aspectRatio = static_cast<float>(mBackbufferWidth) / static_cast<float>(mBackbufferHeight);
	const glm::mat4 projMatrix = glm::perspectiveLH_NO(glm::radians(kMainCameraFovYDegrees), aspectRatio, 0.1f, 20000.0f);
	const glm::mat4 viewProjMatrix = projMatrix * viewMatrix;

	glUseProgram(mTerrainProgram);
	uploadAtmosphereUniforms(mTerrainProgram);
	const int terrainResLoc = getUniformLocationCached(mTerrainProgram, "u_terrain_resolution");
	if (terrainResLoc >= 0) glUniform1i(terrainResLoc, 512);
	const int viewProjLoc = getUniformLocationCached(mTerrainProgram, "u_view_proj");
	if (viewProjLoc >= 0) glUniformMatrix4fv(viewProjLoc, 1, GL_FALSE, glm::value_ptr(viewProjMatrix));
	const int shadowViewProjLoc = getUniformLocationCached(mTerrainProgram, "u_shadow_view_proj");
	if (shadowViewProjLoc >= 0) glUniformMatrix4fv(shadowViewProjLoc, 1, GL_FALSE, mShadowViewProj);
	const int camPosLoc = getUniformLocationCached(mTerrainProgram, "u_camera_world_pos");
	if (camPosLoc >= 0) glUniform3f(camPosLoc, cameraWorld.x, cameraWorld.y, cameraWorld.z);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTerrainHeightmapTex);
	const int heightmapLoc = getUniformLocationCached(mTerrainProgram, "u_heightmap_tex");
	if (heightmapLoc >= 0) glUniform1i(heightmapLoc, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	const int transLoc = getUniformLocationCached(mTerrainProgram, "u_transmittance_lut");
	if (transLoc >= 0) glUniform1i(transLoc, 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, mShadowMapsEnabled ? mShadowDepthTex : mShadowFallbackTex);
	const int shadowTexLoc = getUniformLocationCached(mTerrainProgram, "u_shadowmap_tex");
	if (shadowTexLoc >= 0) glUniform1i(shadowTexLoc, 2);

	glBindVertexArray(mFullscreenVao);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 512 * 512);

	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	endGpuPassTimer(mTerrainPassTimer);
}

void GameGl::renderPresent()
{
	beginGpuPassTimer(mPresentPassTimer);
	glBindFramebuffer(GL_FRAMEBUFFER, mFinalHdrFbo);
	glViewport(0, 0, mBackbufferWidth, mBackbufferHeight);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	const float clearHdr[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	glClearBufferfv(GL_COLOR, 0, clearHdr);

	glUseProgram(mRaymarchProgram);
	uploadAtmosphereUniforms(mRaymarchProgram);
	const int aspectLoc = getUniformLocationCached(mRaymarchProgram, "u_aspect");
	if (aspectLoc >= 0) glUniform1f(aspectLoc, static_cast<float>(mBackbufferWidth) / static_cast<float>(mBackbufferHeight));
	const int fovLoc = getUniformLocationCached(mRaymarchProgram, "u_fov_y_degrees");
	if (fovLoc >= 0) glUniform1f(fovLoc, kMainCameraFovYDegrees);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	const int transLoc = getUniformLocationCached(mRaymarchProgram, "u_transmittance_lut");
	if (transLoc >= 0) glUniform1i(transLoc, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mSkyViewTex);
	const int skyLoc = getUniformLocationCached(mRaymarchProgram, "u_skyview_lut");
	if (skyLoc >= 0) glUniform1i(skyLoc, 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, mMultiScatteringTex);
	const int multiLoc = getUniformLocationCached(mRaymarchProgram, "u_multiscattering_lut");
	if (multiLoc >= 0) glUniform1i(multiLoc, 2);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_3D, mAerialPerspectiveTex);
	const int apLoc = getUniformLocationCached(mRaymarchProgram, "u_aerial_perspective_volume");
	if (apLoc >= 0) glUniform1i(apLoc, 3);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, mSceneHdrTex);
	const int sceneLoc = getUniformLocationCached(mRaymarchProgram, "u_scene_color");
	if (sceneLoc >= 0) glUniform1i(sceneLoc, 4);
	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_2D, mSceneLinearDepthTex);
	const int linearDepthLoc = getUniformLocationCached(mRaymarchProgram, "u_scene_linear_depth");
	if (linearDepthLoc >= 0) glUniform1i(linearDepthLoc, 5);
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, mShadowMapsEnabled ? mShadowDepthTex : mShadowFallbackTex);
	const int shadowTexLoc = getUniformLocationCached(mRaymarchProgram, "u_shadowmap_tex");
	if (shadowTexLoc >= 0) glUniform1i(shadowTexLoc, 6);
	const int shadowViewProjLoc = getUniformLocationCached(mRaymarchProgram, "u_shadow_view_proj");
	if (shadowViewProjLoc >= 0) glUniformMatrix4fv(shadowViewProjLoc, 1, GL_FALSE, mShadowViewProj);
	const int fastSkyLoc = getUniformLocationCached(mRaymarchProgram, "u_fast_sky");
	if (fastSkyLoc >= 0) glUniform1i(fastSkyLoc, mFastSky ? 1 : 0);
	const int fastApLoc = getUniformLocationCached(mRaymarchProgram, "u_fast_aerial_perspective");
	if (fastApLoc >= 0) glUniform1i(fastApLoc, mFastAerialPerspective ? 1 : 0);
	const int apDepthLoc = getUniformLocationCached(mRaymarchProgram, "u_ap_debug_depth_km");
	if (apDepthLoc >= 0) glUniform1f(apDepthLoc, mAerialPerspectiveDebugDepthKm);
	glBindVertexArray(mFullscreenVao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_3D, 0);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, mBackbufferWidth, mBackbufferHeight);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(mPostProcessProgram);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mFinalHdrTex);
	const int hdrLoc = getUniformLocationCached(mPostProcessProgram, "u_hdr_tex");
	if (hdrLoc >= 0) glUniform1i(hdrLoc, 0);
	const int tonemapEnabledLoc = getUniformLocationCached(mPostProcessProgram, "u_enable_tonemap");
	if (tonemapEnabledLoc >= 0) glUniform1i(tonemapEnabledLoc, mPostTonemapEnabled ? 1 : 0);
	const int tonemapModeLoc = getUniformLocationCached(mPostProcessProgram, "u_tonemap_mode");
	if (tonemapModeLoc >= 0) glUniform1i(tonemapModeLoc, mPostTonemapMode);
	const int exposureLoc = getUniformLocationCached(mPostProcessProgram, "u_exposure");
	if (exposureLoc >= 0) glUniform1f(exposureLoc, mPostExposure);
	const int gammaEnabledLoc = getUniformLocationCached(mPostProcessProgram, "u_enable_gamma");
	if (gammaEnabledLoc >= 0) glUniform1i(gammaEnabledLoc, mPostGammaEnabled ? 1 : 0);
	const int gammaLoc = getUniformLocationCached(mPostProcessProgram, "u_output_gamma");
	if (gammaLoc >= 0) glUniform1f(gammaLoc, mPostOutputGamma);
	glBindVertexArray(mFullscreenVao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	endGpuPassTimer(mPresentPassTimer);
}

void GameGl::render()
{
	if (!mInitialised)
	{
		return;
	}

	updateViewAndSunDirections();
	resolveGpuPassTimers();
	renderShadowMap();

	if (mLutDirty)
	{
		renderTransmittanceLut();
		renderMultipleScatteringLut();
		mLutDirty = false;
		mSkyViewDirty = true;
		mAerialPerspectiveDirty = true;
	}
	if (mSkyViewDirty)
	{
		renderSkyViewLut();
		mSkyViewDirty = false;
	}
	if (mAerialPerspectiveDirty)
	{
		renderAerialPerspectiveVolume();
		mAerialPerspectiveDirty = false;
	}
	if (mLutPreviewUpdatesEnabled)
	{
		copyAerialPerspectivePreviewSlice();
		updateLutPreviewTextures();
	}
	renderTerrainScene();
	renderPresent();
}
