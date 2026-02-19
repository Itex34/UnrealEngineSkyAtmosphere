// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct GlVec3
{
	float x, y, z;
};

struct GlAtmosphereInfo
{
	GlVec3 rayleigh_scattering = {};
	GlVec3 mie_scattering = {};
	GlVec3 mie_extinction = {};
	GlVec3 mie_absorption = {};
	GlVec3 absorption_extinction = {};
	GlVec3 ground_albedo = {};
	float bottom_radius = 0.0f;
	float top_radius = 0.0f;
	float mie_phase_g = 0.0f;
	float rayleigh_density_exp_scale = 0.0f;
	float mie_density_exp_scale = 0.0f;
	float absorption_layer0_width = 0.0f;
	float absorption_layer0_linear_term = 0.0f;
	float absorption_layer0_constant_term = 0.0f;
	float absorption_layer1_linear_term = 0.0f;
	float absorption_layer1_constant_term = 0.0f;
};

struct GlLutInfo
{
	unsigned int TRANSMITTANCE_TEXTURE_WIDTH = 256;
	unsigned int TRANSMITTANCE_TEXTURE_HEIGHT = 64;
	unsigned int MULTI_SCATTERING_TEXTURE_SIZE = 32;
	unsigned int SKY_VIEW_TEXTURE_WIDTH = 192;
	unsigned int SKY_VIEW_TEXTURE_HEIGHT = 108;
	unsigned int AERIAL_PERSPECTIVE_TEXTURE_WIDTH = 32;
	unsigned int AERIAL_PERSPECTIVE_TEXTURE_HEIGHT = 32;
	unsigned int AERIAL_PERSPECTIVE_TEXTURE_DEPTH = 32;
};

class GameGl
{
public:
	GameGl() = default;
	~GameGl() = default;

	bool initialise();
	void shutdown();
	void resize(int width, int height);
	void render();
	void setAerialPerspectiveDebug(bool enabled) { mUseAerialPerspectiveDebug = enabled; }
	void setAerialPerspectiveDebugDepthKm(float depthKm) { mAerialPerspectiveDebugDepthKm = depthKm; }
	void setCameraHeight(float value);
	void setCameraForward(float value);
	void setCameraOffset(const GlVec3& value);
	void setViewYaw(float value);
	void setViewPitch(float value);
	void setSunIlluminanceScale(float value);
	void setSunYaw(float value);
	void setSunPitch(float value);
	void setRayMarchMinSpp(int value);
	void setRayMarchMaxSpp(int value);
	void setFastSky(bool enabled);
	void setFastAerialPerspective(bool enabled);
	void setMultipleScatteringFactor(float value);
	void setRenderTerrain(bool enabled) { mRenderTerrain = enabled; }
	void setAtmosphereInfo(const GlAtmosphereInfo& value);

	float getCameraHeight() const { return mCameraHeight; }
	float getCameraForward() const { return mCameraForward; }
	GlVec3 getCameraOffset() const { return mCameraOffset; }
	float getViewYaw() const { return mViewYaw; }
	float getViewPitch() const { return mViewPitch; }
	GlVec3 getViewDir() const { return mViewDir; }
	GlVec3 getViewRight() const { return mViewRight; }
	GlVec3 getViewUp() const { return mViewUp; }
	float getSunIlluminanceScale() const { return mSunIlluminanceScale; }
	float getSunYaw() const { return mSunYaw; }
	float getSunPitch() const { return mSunPitch; }
	int getRayMarchMinSpp() const { return mRayMarchMinSpp; }
	int getRayMarchMaxSpp() const { return mRayMarchMaxSpp; }
	bool getFastSky() const { return mFastSky; }
	bool getFastAerialPerspective() const { return mFastAerialPerspective; }
	float getMultipleScatteringFactor() const { return mMultipleScatteringFactor; }
	bool getRenderTerrain() const { return mRenderTerrain; }
	GlAtmosphereInfo getAtmosphereInfo() const { return mAtmosphereInfo; }

	unsigned int getTransmittanceTexture() const { return mTransmittanceTex; }
	unsigned int getMultipleScatteringTexture() const { return mMultiScatteringTex; }
	unsigned int getSkyViewTexture() const { return mSkyViewTex; }
	float getMultiScatteringDebugMin() const { return mMultiScatteringDebugMin; }
	float getMultiScatteringDebugMax() const { return mMultiScatteringDebugMax; }
	bool hasMultiScatteringDebugStats() const { return mMultiScatteringStatsValid; }
	float getAerialPerspectiveDebugMin() const { return mAerialPerspectiveDebugMin; }
	float getAerialPerspectiveDebugMax() const { return mAerialPerspectiveDebugMax; }
	bool hasAerialPerspectiveDebugStats() const { return mAerialPerspectiveStatsValid; }
	bool isInitialised() const { return mInitialised; }

private:
	bool createPrograms();
	bool createTransmittanceResources();
	bool createMultipleScatteringResources();
	bool createSkyViewResources();
	bool createAerialPerspectiveResources();
	bool createTerrainResources();
	bool createSceneResources();
	void destroyPrograms();
	void destroyTransmittanceResources();
	void destroyMultipleScatteringResources();
	void destroySkyViewResources();
	void destroyAerialPerspectiveResources();
	void destroyTerrainResources();
	void destroySceneResources();
	void renderTransmittanceLut();
	void renderMultipleScatteringLut();
	void renderSkyViewLut();
	void renderAerialPerspectiveVolume();
	void renderTerrainScene();
	void renderPresent();
	void uploadAtmosphereUniforms(unsigned int program);
	void updateMultiScatteringDebugStats();
	void updateAerialPerspectiveDebugStats();
	void updateViewAndSunDirections();
	void markLutsDirty();
	void markSkyAndApDirty();

	unsigned int loadAndCompileShader(unsigned int type, const char* path);
	unsigned int linkProgram(unsigned int vs, unsigned int fs, const char* debugName);
	unsigned int linkComputeProgram(unsigned int cs, const char* debugName);

	bool mInitialised = false;
	bool mLutDirty = true;
	bool mSkyViewDirty = true;
	bool mAerialPerspectiveDirty = true;
	int mBackbufferWidth = 1280;
	int mBackbufferHeight = 720;

	GlLutInfo mLutsInfo;
	GlAtmosphereInfo mAtmosphereInfo = {};
	float mMultipleScatteringFactor = 1.0f;
	float mCameraHeight = 0.5f;
	float mCameraForward = -1.0f;
	float mViewYaw = 0.0f;
	float mViewPitch = 0.0f;
	float mSunIlluminanceScale = 1.0f;
	float mSunYaw = 0.0f;
	float mSunPitch = 0.45f;
	int mRayMarchMinSpp = 4;
	int mRayMarchMaxSpp = 14;
	bool mFastSky = true;
	bool mFastAerialPerspective = true;
	bool mRenderTerrain = true;
	GlVec3 mViewDir = { 0.0f, 1.0f, 0.0f };
	GlVec3 mViewRight = { 1.0f, 0.0f, 0.0f };
	GlVec3 mViewUp = { 0.0f, 0.0f, 1.0f };
	GlVec3 mCameraPosition = { 0.0f, -1.0f, 0.5f };
	GlVec3 mCameraOffset = { 0.0f, -1.0f, 0.5f };
	GlVec3 mSunDir = { 0.0f, 0.70710678f, 0.70710678f };

	unsigned int mFullscreenVao = 0;
	unsigned int mTransmittanceProgram = 0;
	unsigned int mMultiScatteringProgram = 0;
	unsigned int mSkyViewProgram = 0;
	unsigned int mAerialPerspectiveProgram = 0;
	unsigned int mTerrainProgram = 0;
	unsigned int mRaymarchProgram = 0;
	unsigned int mPresentProgram = 0;

	unsigned int mTransmittanceTex = 0;
	unsigned int mTransmittanceFbo = 0;
	unsigned int mMultiScatteringTex = 0;
	unsigned int mSkyViewTex = 0;
	unsigned int mSkyViewFbo = 0;
	unsigned int mAerialPerspectiveTex = 0;
	unsigned int mTerrainHeightmapTex = 0;
	unsigned int mSceneFbo = 0;
	unsigned int mSceneHdrTex = 0;
	unsigned int mSceneLinearDepthTex = 0;
	unsigned int mSceneDepthTex = 0;
	float mMultiScatteringDebugMin = 0.0f;
	float mMultiScatteringDebugMax = 0.0f;
	bool mMultiScatteringStatsValid = false;
	float mAerialPerspectiveDebugMin = 0.0f;
	float mAerialPerspectiveDebugMax = 0.0f;
	bool mAerialPerspectiveStatsValid = false;
	bool mUseAerialPerspectiveDebug = false;
	float mAerialPerspectiveDebugDepthKm = 16.0f;
};
