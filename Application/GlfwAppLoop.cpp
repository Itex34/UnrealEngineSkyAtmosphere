// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlfwAppLoop.h"

#include "GameGl.h"
#include "GlfwAppState.h"
#include "GlfwAppUi.h"

#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include "imgui\examples\imgui_impl_glfw.h"
#include "imgui\examples\imgui_impl_opengl3.h"

namespace
{
	float vecLength(const GlVec3& v)
	{
		return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	}

	void vecNormalize(const GlVec3& v, float len, float out[3])
	{
		if (len > 1e-6f)
		{
			out[0] = v.x / len;
			out[1] = v.y / len;
			out[2] = v.z / len;
		}
		else
		{
			out[0] = 0.0f;
			out[1] = 0.0f;
			out[2] = 0.0f;
		}
	}

	GlVec3 vecScale(const float c[3], float s)
	{
		return { c[0] * s, c[1] * s, c[2] * s };
	}

	void initialiseUiState(GameGl& gameGl, GlfwUiState& state)
	{
		state.uiCamHeight = gameGl.getCameraHeight();
		state.uiCamForward = gameGl.getCameraForward();
		state.uiIllumScale = gameGl.getSunIlluminanceScale();
		state.uiSunYaw = gameGl.getSunYaw();
		state.uiSunPitch = gameGl.getSunPitch();
		state.uiMinSpp = gameGl.getRayMarchMinSpp();
		state.uiMaxSpp = gameGl.getRayMarchMaxSpp();
		state.uiFastSky = gameGl.getFastSky();
		state.uiFastAerialPerspective = gameGl.getFastAerialPerspective();
		state.uiColoredTransmittance = gameGl.getColoredTransmittance();
		state.uiRenderTerrain = gameGl.getRenderTerrain();
		state.uiMultiScattering = gameGl.getMultipleScatteringFactor();
		state.apPreviewSlice = gameGl.getAerialPerspectivePreviewSlice();
		state.uiViewYaw = gameGl.getViewYaw();
		state.uiViewPitch = gameGl.getViewPitch();

		const GlAtmosphereInfo atmosphere = gameGl.getAtmosphereInfo();
		state.uiMiePhase = atmosphere.mie_phase_g;
		state.uiMieScattScale = vecLength(atmosphere.mie_scattering);
		vecNormalize(atmosphere.mie_scattering, state.uiMieScattScale, state.uiMieScattColor);
		state.uiMieAbsScale = vecLength(atmosphere.mie_absorption);
		vecNormalize(atmosphere.mie_absorption, state.uiMieAbsScale, state.uiMieAbsColor);
		state.uiRayScattScale = vecLength(atmosphere.rayleigh_scattering);
		vecNormalize(atmosphere.rayleigh_scattering, state.uiRayScattScale, state.uiRayScattColor);
		state.uiAbsorpScale = vecLength(atmosphere.absorption_extinction);
		vecNormalize(atmosphere.absorption_extinction, state.uiAbsorpScale, state.uiAbsorpColor);
		state.uiBottomRadius = atmosphere.bottom_radius;
		state.uiAtmosphereHeight = atmosphere.top_radius - atmosphere.bottom_radius;
		state.uiMieScaleHeight = -1.0f / atmosphere.mie_density_exp_scale;
		state.uiRayScaleHeight = -1.0f / atmosphere.rayleigh_density_exp_scale;
		state.uiGroundAlbedo[0] = atmosphere.ground_albedo.x;
		state.uiGroundAlbedo[1] = atmosphere.ground_albedo.y;
		state.uiGroundAlbedo[2] = atmosphere.ground_albedo.z;
		state.uiPrevTimeSec = glfwGetTime();
	}

	void updateFrameInputs(GLFWwindow* window, GameGl& gameGl, GlfwUiState& state)
	{
		double nowSec = glfwGetTime();
		float dt = static_cast<float>(nowSec - state.uiPrevTimeSec);
		state.uiPrevTimeSec = nowSec;
		if (dt < 0.0f) dt = 0.0f;
		if (dt > 0.25f) dt = 0.25f;

		const bool f1Down = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
		if (f1Down && !state.f1WasDown)
		{
			state.uiPointerLock = !state.uiPointerLock;
		}
		state.f1WasDown = f1Down;
		if (!state.uiFpsCamera)
		{
			state.uiPointerLock = false;
		}
		if (state.uiPointerLock != state.pointerLockApplied)
		{
			glfwSetInputMode(window, GLFW_CURSOR, state.uiPointerLock ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
			state.pointerLockApplied = state.uiPointerLock;
			state.mouseDeltaInit = false;
		}

		int fbWidth = 0;
		int fbHeight = 0;
		glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
		gameGl.resize(fbWidth, fbHeight);

		if (state.uiAnimateSun)
		{
			state.uiSunPitch += state.uiSunAnimSpeed * dt;
			if (state.uiSunPitch > 3.14159265f) state.uiSunPitch -= 6.2831853f;
			if (state.uiSunPitch < -3.14159265f) state.uiSunPitch += 6.2831853f;
		}

		gameGl.setViewYaw(state.uiViewYaw);
		gameGl.setViewPitch(state.uiViewPitch);

		if (state.uiFpsCamera)
		{
			if (state.uiPointerLock)
			{
				double mouseX = 0.0;
				double mouseY = 0.0;
				glfwGetCursorPos(window, &mouseX, &mouseY);
				if (!state.mouseDeltaInit)
				{
					state.lastMouseX = mouseX;
					state.lastMouseY = mouseY;
					state.mouseDeltaInit = true;
				}
				else
				{
					const double deltaX = mouseX - state.lastMouseX;
					const double deltaY = mouseY - state.lastMouseY;
					state.lastMouseX = mouseX;
					state.lastMouseY = mouseY;
					state.uiViewYaw += static_cast<float>(deltaX) * state.uiMouseSensitivity;
					state.uiViewPitch -= static_cast<float>(deltaY) * state.uiMouseSensitivity;
					if (state.uiViewPitch > 1.55f) state.uiViewPitch = 1.55f;
					if (state.uiViewPitch < -1.55f) state.uiViewPitch = -1.55f;
					gameGl.setViewYaw(state.uiViewYaw);
					gameGl.setViewPitch(state.uiViewPitch);
				}
			}
			else
			{
				state.mouseDeltaInit = false;
			}

			GlVec3 camera = gameGl.getCameraOffset();
			const GlVec3 forward = gameGl.getViewDir();
			const GlVec3 right = gameGl.getViewRight();
			const GlVec3 up = { 0.0f, 0.0f, 1.0f };
			GlVec3 move = { 0.0f, 0.0f, 0.0f };
			if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { move.x += forward.x; move.y += forward.y; move.z += forward.z; }
			if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { move.x -= forward.x; move.y -= forward.y; move.z -= forward.z; }
			if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { move.x += right.x; move.y += right.y; move.z += right.z; }
			if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { move.x -= right.x; move.y -= right.y; move.z -= right.z; }
			if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) { move.x += up.x; move.y += up.y; move.z += up.z; }
			if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) { move.x -= up.x; move.y -= up.y; move.z -= up.z; }

			const float moveLen = std::sqrt(move.x * move.x + move.y * move.y + move.z * move.z);
			if (moveLen > 1e-6f)
			{
				move.x /= moveLen;
				move.y /= moveLen;
				move.z /= moveLen;
				float speed = state.uiCameraMoveSpeed;
				if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
				{
					speed *= 3.0f;
				}
				camera.x += move.x * speed * dt;
				camera.y += move.y * speed * dt;
				camera.z += move.z * speed * dt;
				gameGl.setCameraOffset(camera);
			}

			const GlVec3 cameraNow = gameGl.getCameraOffset();
			state.uiCamForward = cameraNow.y;
			state.uiCamHeight = cameraNow.z;
		}
		else
		{
			gameGl.setCameraHeight(state.uiCamHeight);
			gameGl.setCameraForward(state.uiCamForward);
		}

		gameGl.setSunIlluminanceScale(state.uiIllumScale);
		gameGl.setSunYaw(state.uiSunYaw);
		gameGl.setSunPitch(state.uiSunPitch);
		gameGl.setRayMarchMinSpp(state.uiMinSpp);
		gameGl.setRayMarchMaxSpp(state.uiMaxSpp);
		gameGl.setFastSky(state.uiFastSky);
		gameGl.setFastAerialPerspective(state.uiFastAerialPerspective);
		gameGl.setColoredTransmittance(state.uiColoredTransmittance && !state.uiFastAerialPerspective);
		gameGl.setRenderTerrain(state.uiRenderTerrain);
		gameGl.setMultipleScatteringFactor(state.uiMultiScattering);
		gameGl.setAerialPerspectiveDebugDepthKm(state.apDebugDepthKm);
		gameGl.setAerialPerspectivePreviewSlice(state.apPreviewSlice);
		gameGl.setMultiScatteringPreviewExposure(state.msPreviewExposure);
		gameGl.setAerialPerspectivePreviewExposure(state.apPreviewExposure);
		if (state.applyAtmosphereUi)
		{
			GlAtmosphereInfo atmosphere = gameGl.getAtmosphereInfo();
			atmosphere.mie_phase_g = state.uiMiePhase;
			atmosphere.mie_scattering = vecScale(state.uiMieScattColor, state.uiMieScattScale);
			atmosphere.mie_absorption = vecScale(state.uiMieAbsColor, state.uiMieAbsScale);
			atmosphere.mie_extinction = {
				atmosphere.mie_scattering.x + atmosphere.mie_absorption.x,
				atmosphere.mie_scattering.y + atmosphere.mie_absorption.y,
				atmosphere.mie_scattering.z + atmosphere.mie_absorption.z
			};
			atmosphere.rayleigh_scattering = vecScale(state.uiRayScattColor, state.uiRayScattScale);
			atmosphere.absorption_extinction = vecScale(state.uiAbsorpColor, state.uiAbsorpScale);
			atmosphere.bottom_radius = state.uiBottomRadius;
			atmosphere.top_radius = state.uiBottomRadius + state.uiAtmosphereHeight;
			atmosphere.mie_density_exp_scale = -1.0f / (state.uiMieScaleHeight > 0.001f ? state.uiMieScaleHeight : 0.001f);
			atmosphere.rayleigh_density_exp_scale = -1.0f / (state.uiRayScaleHeight > 0.001f ? state.uiRayScaleHeight : 0.001f);
			atmosphere.ground_albedo = { state.uiGroundAlbedo[0], state.uiGroundAlbedo[1], state.uiGroundAlbedo[2] };
			gameGl.setAtmosphereInfo(atmosphere);
			state.applyAtmosphereUi = false;
		}
		state.uiMinSpp = gameGl.getRayMarchMinSpp();
		state.uiMaxSpp = gameGl.getRayMarchMaxSpp();
	}
}

void runGlfwMainLoop(GLFWwindow* window, GameGl& gameGl)
{
	GlfwUiState state;
	initialiseUiState(gameGl, state);

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		{
			glfwSetWindowShouldClose(window, GLFW_TRUE);
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		updateFrameInputs(window, gameGl, state);
		gameGl.render();
		drawGlfwUi(gameGl, state);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window);
	}
}
