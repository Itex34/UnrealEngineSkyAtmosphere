// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct GlfwUiState
{
	float msPreviewExposure = 32.0f;
	float apPreviewExposure = 16.0f;
	int apPreviewSlice = 16;
	bool uiLutPreviewVisible = true;
	float uiCamHeight = 0.5f;
	float uiCamForward = -1.0f;
	bool uiPostTonemapEnabled = true;
	int uiTonemapMode = 0;
	float uiPostExposure = 10.0f;
	bool uiPostGammaEnabled = true;
	float uiPostOutputGamma = 2.2f;
	float uiIllumScale = 1.0f;
	float uiSunYaw = 0.0f;
	float uiSunPitch = 0.45f;
	int uiMinSpp = 4;
	int uiMaxSpp = 14;
	bool uiFastSky = true;
	bool uiFastAerialPerspective = true;
	bool uiShadowMaps = true;
	bool uiColoredTransmittance = false;
	bool uiRenderTerrain = true;
	float uiMultiScattering = 1.0f;
	float apDebugDepthKm = 16.0f;
	bool uiAnimateSun = false;
	float uiSunAnimSpeed = 0.20f;
	bool uiFpsCamera = false;
	bool uiPointerLock = false;
	float uiCameraMoveSpeed = 40.0f;
	float uiMouseSensitivity = 0.0025f;
	float uiViewYaw = 0.0f;
	float uiViewPitch = 0.0f;
	double uiPrevTimeSec = 0.0;
	bool pointerLockApplied = false;
	bool f1WasDown = false;
	bool mouseDeltaInit = false;
	double lastMouseX = 0.0;
	double lastMouseY = 0.0;
	float uiMiePhase = 0.8f;
	float uiMieScattColor[3] = { 1.0f, 1.0f, 1.0f };
	float uiMieScattScale = 0.0f;
	float uiMieAbsColor[3] = { 1.0f, 1.0f, 1.0f };
	float uiMieAbsScale = 0.0f;
	float uiRayScattColor[3] = { 1.0f, 1.0f, 1.0f };
	float uiRayScattScale = 0.0f;
	float uiAbsorpColor[3] = { 1.0f, 1.0f, 1.0f };
	float uiAbsorpScale = 0.0f;
	float uiBottomRadius = 6360.0f;
	float uiAtmosphereHeight = 100.0f;
	float uiMieScaleHeight = 1.2f;
	float uiRayScaleHeight = 8.0f;
	float uiGroundAlbedo[3] = { 0.0f, 0.0f, 0.0f };
	bool applyAtmosphereUi = false;
};
