// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameGl.h"

#include <windows.h>
#include <glad/glad.h>
#include <DirectXMath.h>
#include <tinyexr/tinyexr.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace DirectX;

namespace
{
	const char* kFullscreenVertexShaderPath = "Resources/glsl/fullscreen_triangle.vert";
	const char* kTransmittanceFragmentShaderPath = "Resources/glsl/transmittance_lut.frag";
	const char* kMultiScatteringComputeShaderPath = "Resources/glsl/new_multi_scattering_lut.comp";
	const char* kSkyViewFragmentShaderPath = "Resources/glsl/skyview_lut.frag";
	const char* kAerialPerspectiveComputeShaderPath = "Resources/glsl/aerial_perspective_volume.comp";
	const char* kTerrainVertexShaderPath = "Resources/glsl/terrain.vert";
	const char* kTerrainFragmentShaderPath = "Resources/glsl/terrain.frag";
	const char* kRaymarchFragmentShaderPath = "Resources/glsl/render_raymarching_hillaire.frag";
	const char* kPresentFragmentShaderPath = "Resources/glsl/render_sky_from_lut.frag";

	std::string readTextFile(const char* path)
	{
		const std::string originalPath(path);
		const std::string candidates[] = {
			originalPath,
			"..\\" + originalPath,
			"..\\..\\" + originalPath,
			"..\\..\\..\\" + originalPath,
			"..\\..\\..\\..\\" + originalPath,
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
		MessageBoxA(nullptr, message.c_str(), title, MB_ICONERROR | MB_OK);
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
			"..\\" + originalPath,
			"..\\..\\" + originalPath,
			"..\\..\\..\\" + originalPath,
			"..\\..\\..\\..\\" + originalPath,
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

void GameGl::setCameraHeight(float value)
{
	if (std::fabs(mCameraHeight - value) > 1e-6f)
	{
		mCameraHeight = value;
		mCameraPosition.z = value;
		markSkyAndApDirty();
	}
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
		markSkyAndApDirty();
	}
}

void GameGl::setSunPitch(float value)
{
	if (std::fabs(mSunPitch - value) > 1e-6f)
	{
		mSunPitch = value;
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
	// Same base orientation convention as the DX11 sample (Z-up, forward +Y).
	const XMMATRIX viewMatrix = XMMatrixRotationRollPitchYaw(-mViewPitch, mViewYaw, 0.0f);
	XMFLOAT3 viewDirDx = {};
	XMStoreFloat3(&viewDirDx, viewMatrix.r[2]);
	mViewDir = { viewDirDx.x, viewDirDx.z, viewDirDx.y };
	const float viewLen = std::sqrt(mViewDir.x * mViewDir.x + mViewDir.y * mViewDir.y + mViewDir.z * mViewDir.z);
	if (viewLen > 1e-6f)
	{
		mViewDir = { mViewDir.x / viewLen, mViewDir.y / viewLen, mViewDir.z / viewLen };
	}
	else
	{
		mViewDir = { 0.0f, 1.0f, 0.0f };
	}
	const GlVec3 worldUp = { 0.0f, 0.0f, 1.0f };
	GlVec3 right = {
		worldUp.y * mViewDir.z - worldUp.z * mViewDir.y,
		worldUp.z * mViewDir.x - worldUp.x * mViewDir.z,
		worldUp.x * mViewDir.y - worldUp.y * mViewDir.x
	};
	float rightLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
	if (rightLen < 1e-6f)
	{
		right = { 1.0f, 0.0f, 0.0f };
		rightLen = 1.0f;
	}
	mViewRight = { right.x / rightLen, right.y / rightLen, right.z / rightLen };
	mViewUp = {
		mViewDir.y * mViewRight.z - mViewDir.z * mViewRight.y,
		mViewDir.z * mViewRight.x - mViewDir.x * mViewRight.z,
		mViewDir.x * mViewRight.y - mViewDir.y * mViewRight.x
	};
	const float upLen = std::sqrt(mViewUp.x * mViewUp.x + mViewUp.y * mViewUp.y + mViewUp.z * mViewUp.z);
	if (upLen > 1e-6f)
	{
		mViewUp = { mViewUp.x / upLen, mViewUp.y / upLen, mViewUp.z / upLen };
	}
	else
	{
		mViewUp = { 0.0f, 0.0f, 1.0f };
	}
	mCameraOffset = mCameraPosition;
	mCameraForward = mCameraPosition.y;
	mCameraHeight = mCameraPosition.z;

	const XMMATRIX sunMatrix = XMMatrixRotationRollPitchYaw(-mSunPitch, mSunYaw, 0.0f);
	XMFLOAT3 sunDirDx = {};
	XMStoreFloat3(&sunDirDx, sunMatrix.r[2]);
	GlVec3 sunDirZUp = { sunDirDx.x, sunDirDx.z, sunDirDx.y };
	const float len = std::sqrt(sunDirZUp.x * sunDirZUp.x + sunDirZUp.y * sunDirZUp.y + sunDirZUp.z * sunDirZUp.z);
	if (len > 1e-6f)
	{
		mSunDir = { sunDirZUp.x / len, sunDirZUp.y / len, sunDirZUp.z / len };
	}
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
	const unsigned int raymarchFs = loadAndCompileShader(GL_FRAGMENT_SHADER, kRaymarchFragmentShaderPath);
	const unsigned int presentFs = loadAndCompileShader(GL_FRAGMENT_SHADER, kPresentFragmentShaderPath);

	if (fullscreenVs == 0 || transmittanceFs == 0 || multiScatteringCs == 0 || skyViewFs == 0 || aerialPerspectiveCs == 0 || terrainVs == 0 || terrainFs == 0 || raymarchFs == 0 || presentFs == 0)
	{
		if (fullscreenVs != 0) glDeleteShader(fullscreenVs);
		if (transmittanceFs != 0) glDeleteShader(transmittanceFs);
		if (multiScatteringCs != 0) glDeleteShader(multiScatteringCs);
		if (skyViewFs != 0) glDeleteShader(skyViewFs);
		if (aerialPerspectiveCs != 0) glDeleteShader(aerialPerspectiveCs);
		if (terrainVs != 0) glDeleteShader(terrainVs);
		if (terrainFs != 0) glDeleteShader(terrainFs);
		if (raymarchFs != 0) glDeleteShader(raymarchFs);
		if (presentFs != 0) glDeleteShader(presentFs);
		return false;
	}

	mTransmittanceProgram = linkProgram(fullscreenVs, transmittanceFs, "TransmittanceLut");
	mMultiScatteringProgram = linkComputeProgram(multiScatteringCs, "NewMultiScatteringLut");
	mSkyViewProgram = linkProgram(fullscreenVs, skyViewFs, "SkyViewLut");
	mAerialPerspectiveProgram = linkComputeProgram(aerialPerspectiveCs, "AerialPerspectiveVolume");
	mTerrainProgram = linkProgram(terrainVs, terrainFs, "Terrain");
	mRaymarchProgram = linkProgram(fullscreenVs, raymarchFs, "RenderRaymarchingHillaire");
	mPresentProgram = linkProgram(fullscreenVs, presentFs, "RenderSkyFromLut");

	glDeleteShader(fullscreenVs);
	glDeleteShader(transmittanceFs);
	glDeleteShader(multiScatteringCs);
	glDeleteShader(skyViewFs);
	glDeleteShader(aerialPerspectiveCs);
	glDeleteShader(terrainVs);
	glDeleteShader(terrainFs);
	glDeleteShader(raymarchFs);
	glDeleteShader(presentFs);

	if (mTransmittanceProgram == 0 || mMultiScatteringProgram == 0 || mSkyViewProgram == 0 || mAerialPerspectiveProgram == 0 || mTerrainProgram == 0 || mRaymarchProgram == 0 || mPresentProgram == 0)
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
		static_cast<GLsizei>(mLutsInfo.MULTI_SCATTERING_TEXTURE_SIZE),
		static_cast<GLsizei>(mLutsInfo.MULTI_SCATTERING_TEXTURE_SIZE),
		0,
		GL_RGBA,
		GL_FLOAT,
		nullptr);
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
		static_cast<GLsizei>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_WIDTH),
		static_cast<GLsizei>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_HEIGHT),
		static_cast<GLsizei>(mLutsInfo.AERIAL_PERSPECTIVE_TEXTURE_DEPTH),
		0,
		GL_RGBA,
		GL_FLOAT,
		nullptr);
	glBindTexture(GL_TEXTURE_3D, 0);
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
	return true;
}

void GameGl::destroyPrograms()
{
	if (mPresentProgram != 0)
	{
		glDeleteProgram(mPresentProgram);
		mPresentProgram = 0;
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

void GameGl::destroySceneResources()
{
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
	glGenVertexArrays(1, &mFullscreenVao);

	if (!createPrograms())
	{
		shutdown();
		return false;
	}
	if (!createTransmittanceResources() || !createMultipleScatteringResources() || !createSkyViewResources() || !createAerialPerspectiveResources() || !createTerrainResources() || !createSceneResources())
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
	destroySceneResources();
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
		createSceneResources();
		mAerialPerspectiveDirty = true;
	}
}

void GameGl::uploadAtmosphereUniforms(unsigned int program)
{
	const auto set1f = [&](const char* name, float value)
	{
		const int loc = glGetUniformLocation(program, name);
		if (loc >= 0) glUniform1f(loc, value);
	};
	const auto set1i = [&](const char* name, int value)
	{
		const int loc = glGetUniformLocation(program, name);
		if (loc >= 0) glUniform1i(loc, value);
	};
	const auto set3f = [&](const char* name, const GlVec3& v)
	{
		const int loc = glGetUniformLocation(program, name);
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
	set1f("u_camera_height", mCameraHeight);
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
	set3f("u_camera_offset", mCameraOffset);
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
}

void GameGl::renderTransmittanceLut()
{
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
}

void GameGl::renderMultipleScatteringLut()
{
	glUseProgram(mMultiScatteringProgram);
	uploadAtmosphereUniforms(mMultiScatteringProgram);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	const int transLoc = glGetUniformLocation(mMultiScatteringProgram, "u_transmittance_lut");
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

	updateMultiScatteringDebugStats();
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
	glUseProgram(mAerialPerspectiveProgram);
	uploadAtmosphereUniforms(mAerialPerspectiveProgram);
	const int aspectLoc = glGetUniformLocation(mAerialPerspectiveProgram, "u_aspect");
	if (aspectLoc >= 0) glUniform1f(aspectLoc, static_cast<float>(mBackbufferWidth) / static_cast<float>(mBackbufferHeight));
	const int fovLoc = glGetUniformLocation(mAerialPerspectiveProgram, "u_fov_y_degrees");
	if (fovLoc >= 0) glUniform1f(fovLoc, 60.0f);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	const int transLoc = glGetUniformLocation(mAerialPerspectiveProgram, "u_transmittance_lut");
	if (transLoc >= 0) glUniform1i(transLoc, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mMultiScatteringTex);
	const int multiLoc = glGetUniformLocation(mAerialPerspectiveProgram, "u_multiscattering_lut");
	if (multiLoc >= 0) glUniform1i(multiLoc, 1);

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
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);

	updateAerialPerspectiveDebugStats();
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
	glBindFramebuffer(GL_FRAMEBUFFER, mSkyViewFbo);
	glViewport(0, 0, static_cast<GLsizei>(mLutsInfo.SKY_VIEW_TEXTURE_WIDTH), static_cast<GLsizei>(mLutsInfo.SKY_VIEW_TEXTURE_HEIGHT));
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	glUseProgram(mSkyViewProgram);
	uploadAtmosphereUniforms(mSkyViewProgram);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	const int transLoc = glGetUniformLocation(mSkyViewProgram, "u_transmittance_lut");
	if (transLoc >= 0) glUniform1i(transLoc, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mMultiScatteringTex);
	const int multiLoc = glGetUniformLocation(mSkyViewProgram, "u_multiscattering_lut");
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
}

void GameGl::renderTerrainScene()
{
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
		return;
	}

	const GlVec3 cameraWorld = {
		mCameraOffset.x,
		mCameraOffset.y,
		mCameraOffset.z
	};
	const XMVECTOR eyePosition = XMVectorSet(cameraWorld.x, cameraWorld.y, cameraWorld.z, 1.0f);
	const XMVECTOR focusPosition = XMVectorSet(cameraWorld.x + mViewDir.x, cameraWorld.y + mViewDir.y, cameraWorld.z + mViewDir.z, 1.0f);
	const XMVECTOR upDirection = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	const XMMATRIX viewMatrix = XMMatrixLookAtLH(eyePosition, focusPosition, upDirection);
	const float aspectRatio = static_cast<float>(mBackbufferWidth) / static_cast<float>(mBackbufferHeight);
	const XMMATRIX projMatrix = XMMatrixPerspectiveFovLH(66.6f * (XM_PI / 180.0f), aspectRatio, 0.1f, 20000.0f);
	const XMMATRIX viewProjMatrix = XMMatrixMultiply(viewMatrix, projMatrix);
	XMFLOAT4X4 viewProj = {};
	XMStoreFloat4x4(&viewProj, viewProjMatrix);

	glUseProgram(mTerrainProgram);
	uploadAtmosphereUniforms(mTerrainProgram);
	const int terrainResLoc = glGetUniformLocation(mTerrainProgram, "u_terrain_resolution");
	if (terrainResLoc >= 0) glUniform1i(terrainResLoc, 512);
	const int viewProjLoc = glGetUniformLocation(mTerrainProgram, "u_view_proj");
	if (viewProjLoc >= 0) glUniformMatrix4fv(viewProjLoc, 1, GL_TRUE, &viewProj.m[0][0]);
	const int camPosLoc = glGetUniformLocation(mTerrainProgram, "u_camera_world_pos");
	if (camPosLoc >= 0) glUniform3f(camPosLoc, cameraWorld.x, cameraWorld.y, cameraWorld.z);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTerrainHeightmapTex);
	const int heightmapLoc = glGetUniformLocation(mTerrainProgram, "u_heightmap_tex");
	if (heightmapLoc >= 0) glUniform1i(heightmapLoc, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	const int transLoc = glGetUniformLocation(mTerrainProgram, "u_transmittance_lut");
	if (transLoc >= 0) glUniform1i(transLoc, 1);

	glBindVertexArray(mFullscreenVao);
	glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 512 * 512);

	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GameGl::renderPresent()
{
	renderTerrainScene();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, mBackbufferWidth, mBackbufferHeight);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(mRaymarchProgram);
	uploadAtmosphereUniforms(mRaymarchProgram);
	const int aspectLoc = glGetUniformLocation(mRaymarchProgram, "u_aspect");
	if (aspectLoc >= 0) glUniform1f(aspectLoc, static_cast<float>(mBackbufferWidth) / static_cast<float>(mBackbufferHeight));
	const int fovLoc = glGetUniformLocation(mRaymarchProgram, "u_fov_y_degrees");
	if (fovLoc >= 0) glUniform1f(fovLoc, 60.0f);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mTransmittanceTex);
	const int transLoc = glGetUniformLocation(mRaymarchProgram, "u_transmittance_lut");
	if (transLoc >= 0) glUniform1i(transLoc, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, mSkyViewTex);
	const int skyLoc = glGetUniformLocation(mRaymarchProgram, "u_skyview_lut");
	if (skyLoc >= 0) glUniform1i(skyLoc, 1);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, mMultiScatteringTex);
	const int multiLoc = glGetUniformLocation(mRaymarchProgram, "u_multiscattering_lut");
	if (multiLoc >= 0) glUniform1i(multiLoc, 2);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_3D, mAerialPerspectiveTex);
	const int apLoc = glGetUniformLocation(mRaymarchProgram, "u_aerial_perspective_volume");
	if (apLoc >= 0) glUniform1i(apLoc, 3);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, mSceneHdrTex);
	const int sceneLoc = glGetUniformLocation(mRaymarchProgram, "u_scene_color");
	if (sceneLoc >= 0) glUniform1i(sceneLoc, 4);
	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_2D, mSceneLinearDepthTex);
	const int linearDepthLoc = glGetUniformLocation(mRaymarchProgram, "u_scene_linear_depth");
	if (linearDepthLoc >= 0) glUniform1i(linearDepthLoc, 5);
	const int fastSkyLoc = glGetUniformLocation(mRaymarchProgram, "u_fast_sky");
	if (fastSkyLoc >= 0) glUniform1i(fastSkyLoc, mFastSky ? 1 : 0);
	const int fastApLoc = glGetUniformLocation(mRaymarchProgram, "u_fast_aerial_perspective");
	if (fastApLoc >= 0) glUniform1i(fastApLoc, mFastAerialPerspective ? 1 : 0);
	const int apDepthLoc = glGetUniformLocation(mRaymarchProgram, "u_ap_debug_depth_km");
	if (apDepthLoc >= 0) glUniform1f(apDepthLoc, mAerialPerspectiveDebugDepthKm);
	glBindVertexArray(mFullscreenVao);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	glBindVertexArray(0);
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
}

void GameGl::render()
{
	if (!mInitialised)
	{
		return;
	}

	updateViewAndSunDirections();

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
	renderPresent();
}
