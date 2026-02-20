// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuildConfig.h"

#if SKY_OPENGL_EXPERIMENT

#include "GameGl.h"

#include <windows.h>
#include <cstdint>
#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <imgui.h>
#include "imgui\examples\imgui_impl_glfw.h"
#include "imgui\examples\imgui_impl_opengl3.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	(void)hInstance;
	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nCmdShow;

	if (!glfwInit())
	{
		MessageBoxA(nullptr, "glfwInit() failed.", "OpenGL bootstrap error", MB_ICONERROR | MB_OK);
		return -1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

	GLFWwindow* window = glfwCreateWindow(1280, 720, "Sky Atmosphere - OpenGL (WIP)", nullptr, nullptr);
	if (!window)
	{
		glfwTerminate();
		MessageBoxA(nullptr, "glfwCreateWindow() failed.", "OpenGL bootstrap error", MB_ICONERROR | MB_OK);
		return -1;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		glfwDestroyWindow(window);
		glfwTerminate();
		MessageBoxA(nullptr, "gladLoadGLLoader() failed.", "OpenGL bootstrap error", MB_ICONERROR | MB_OK);
		return -1;
	}

	ImGuiContext* imguiContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(imguiContext);
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 430");

	GameGl gameGl;
	if (!gameGl.initialise())
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext(imguiContext);
		glfwDestroyWindow(window);
		glfwTerminate();
		return -1;
	}

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
		static float msPreviewExposure = 32.0f;
		static bool uiInit = false;
		static float uiCamHeight = 0.5f;
		static float uiCamForward = -1.0f;
		static float uiIllumScale = 1.0f;
		static float uiSunYaw = 0.0f;
		static float uiSunPitch = 0.45f;
		static int uiMinSpp = 4;
		static int uiMaxSpp = 14;
		static bool uiFastSky = true;
		static bool uiFastAerialPerspective = true;
		static bool uiRenderTerrain = true;
		static float uiMultiScattering = 1.0f;
		static float apDebugDepthKm = 16.0f;
		static bool uiAnimateSun = false;
		static float uiSunAnimSpeed = 0.20f;
		static bool uiFpsCamera = false;
		static bool uiPointerLock = false;
		static float uiCameraMoveSpeed = 40.0f;
		static float uiMouseSensitivity = 0.0025f;
		static float uiViewYaw = 0.0f;
		static float uiViewPitch = 0.0f;
		static double uiPrevTimeSec = glfwGetTime();
		static bool pointerLockApplied = false;
		static bool f1WasDown = false;
		static bool mouseDeltaInit = false;
		static double lastMouseX = 0.0;
		static double lastMouseY = 0.0;
		static float uiMiePhase = 0.8f;
		static float uiMieScattColor[3] = { 1.0f, 1.0f, 1.0f };
		static float uiMieScattScale = 0.0f;
		static float uiMieAbsColor[3] = { 1.0f, 1.0f, 1.0f };
		static float uiMieAbsScale = 0.0f;
		static float uiRayScattColor[3] = { 1.0f, 1.0f, 1.0f };
		static float uiRayScattScale = 0.0f;
		static float uiAbsorpColor[3] = { 1.0f, 1.0f, 1.0f };
		static float uiAbsorpScale = 0.0f;
		static float uiBottomRadius = 6360.0f;
		static float uiAtmosphereHeight = 100.0f;
		static float uiMieScaleHeight = 1.2f;
		static float uiRayScaleHeight = 8.0f;
		static float uiGroundAlbedo[3] = { 0.0f, 0.0f, 0.0f };
		static bool applyAtmosphereUi = false;

		if (!uiInit)
		{
			uiCamHeight = gameGl.getCameraHeight();
			uiCamForward = gameGl.getCameraForward();
			uiIllumScale = gameGl.getSunIlluminanceScale();
			uiSunYaw = gameGl.getSunYaw();
			uiSunPitch = gameGl.getSunPitch();
			uiMinSpp = gameGl.getRayMarchMinSpp();
			uiMaxSpp = gameGl.getRayMarchMaxSpp();
			uiFastSky = gameGl.getFastSky();
			uiFastAerialPerspective = gameGl.getFastAerialPerspective();
			uiRenderTerrain = gameGl.getRenderTerrain();
			uiMultiScattering = gameGl.getMultipleScatteringFactor();
			uiViewYaw = gameGl.getViewYaw();
			uiViewPitch = gameGl.getViewPitch();

			const GlAtmosphereInfo atmosphere = gameGl.getAtmosphereInfo();
			auto vecLength = [](const GlVec3& v) -> float
			{
				return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
			};
			auto vecNormalize = [&](const GlVec3& v, float len, float out[3])
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
			};

			uiMiePhase = atmosphere.mie_phase_g;
			uiMieScattScale = vecLength(atmosphere.mie_scattering);
			vecNormalize(atmosphere.mie_scattering, uiMieScattScale, uiMieScattColor);
			uiMieAbsScale = vecLength(atmosphere.mie_absorption);
			vecNormalize(atmosphere.mie_absorption, uiMieAbsScale, uiMieAbsColor);
			uiRayScattScale = vecLength(atmosphere.rayleigh_scattering);
			vecNormalize(atmosphere.rayleigh_scattering, uiRayScattScale, uiRayScattColor);
			uiAbsorpScale = vecLength(atmosphere.absorption_extinction);
			vecNormalize(atmosphere.absorption_extinction, uiAbsorpScale, uiAbsorpColor);
			uiBottomRadius = atmosphere.bottom_radius;
			uiAtmosphereHeight = atmosphere.top_radius - atmosphere.bottom_radius;
			uiMieScaleHeight = -1.0f / atmosphere.mie_density_exp_scale;
			uiRayScaleHeight = -1.0f / atmosphere.rayleigh_density_exp_scale;
			uiGroundAlbedo[0] = atmosphere.ground_albedo.x;
			uiGroundAlbedo[1] = atmosphere.ground_albedo.y;
			uiGroundAlbedo[2] = atmosphere.ground_albedo.z;
			uiInit = true;
		}

		const double nowSec = glfwGetTime();
		float dt = static_cast<float>(nowSec - uiPrevTimeSec);
		uiPrevTimeSec = nowSec;
		if (dt < 0.0f) dt = 0.0f;
		if (dt > 0.25f) dt = 0.25f;

		const bool f1Down = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
		if (f1Down && !f1WasDown)
		{
			uiPointerLock = !uiPointerLock;
		}
		f1WasDown = f1Down;
		if (!uiFpsCamera)
		{
			uiPointerLock = false;
		}
		if (uiPointerLock != pointerLockApplied)
		{
			glfwSetInputMode(window, GLFW_CURSOR, uiPointerLock ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
			pointerLockApplied = uiPointerLock;
			mouseDeltaInit = false;
		}

		int fbWidth = 0;
		int fbHeight = 0;
		glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
		gameGl.resize(fbWidth, fbHeight);

		if (uiAnimateSun)
		{
			uiSunPitch += uiSunAnimSpeed * dt;
			if (uiSunPitch > 3.14159265f) uiSunPitch -= 6.2831853f;
			if (uiSunPitch < -3.14159265f) uiSunPitch += 6.2831853f;
		}

		gameGl.setViewYaw(uiViewYaw);
		gameGl.setViewPitch(uiViewPitch);

		if (uiFpsCamera)
		{
			if (uiPointerLock)
			{
				double mouseX = 0.0;
				double mouseY = 0.0;
				glfwGetCursorPos(window, &mouseX, &mouseY);
				if (!mouseDeltaInit)
				{
					lastMouseX = mouseX;
					lastMouseY = mouseY;
					mouseDeltaInit = true;
				}
				else
				{
					const double deltaX = mouseX - lastMouseX;
					const double deltaY = mouseY - lastMouseY;
					lastMouseX = mouseX;
					lastMouseY = mouseY;
					uiViewYaw += static_cast<float>(deltaX) * uiMouseSensitivity;
					uiViewPitch -= static_cast<float>(deltaY) * uiMouseSensitivity;
					if (uiViewPitch > 1.55f) uiViewPitch = 1.55f;
					if (uiViewPitch < -1.55f) uiViewPitch = -1.55f;
					gameGl.setViewYaw(uiViewYaw);
					gameGl.setViewPitch(uiViewPitch);
				}
			}
			else
			{
				mouseDeltaInit = false;
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
				float speed = uiCameraMoveSpeed;
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
			uiCamForward = cameraNow.y;
			uiCamHeight = cameraNow.z;
		}
		else
		{
			gameGl.setCameraHeight(uiCamHeight);
			gameGl.setCameraForward(uiCamForward);
		}

		gameGl.setSunIlluminanceScale(uiIllumScale);
		gameGl.setSunYaw(uiSunYaw);
		gameGl.setSunPitch(uiSunPitch);
		gameGl.setRayMarchMinSpp(uiMinSpp);
		gameGl.setRayMarchMaxSpp(uiMaxSpp);
		gameGl.setFastSky(uiFastSky);
		gameGl.setFastAerialPerspective(uiFastAerialPerspective);
		gameGl.setRenderTerrain(uiRenderTerrain);
		gameGl.setMultipleScatteringFactor(uiMultiScattering);
		gameGl.setAerialPerspectiveDebugDepthKm(apDebugDepthKm);
		if (applyAtmosphereUi)
		{
			auto vecScale = [](const float c[3], float s) -> GlVec3
			{
				return { c[0] * s, c[1] * s, c[2] * s };
			};

			GlAtmosphereInfo atmosphere = gameGl.getAtmosphereInfo();
			atmosphere.mie_phase_g = uiMiePhase;
			atmosphere.mie_scattering = vecScale(uiMieScattColor, uiMieScattScale);
			atmosphere.mie_absorption = vecScale(uiMieAbsColor, uiMieAbsScale);
			atmosphere.mie_extinction = {
				atmosphere.mie_scattering.x + atmosphere.mie_absorption.x,
				atmosphere.mie_scattering.y + atmosphere.mie_absorption.y,
				atmosphere.mie_scattering.z + atmosphere.mie_absorption.z
			};
			atmosphere.rayleigh_scattering = vecScale(uiRayScattColor, uiRayScattScale);
			atmosphere.absorption_extinction = vecScale(uiAbsorpColor, uiAbsorpScale);
			atmosphere.bottom_radius = uiBottomRadius;
			atmosphere.top_radius = uiBottomRadius + uiAtmosphereHeight;
			atmosphere.mie_density_exp_scale = -1.0f / (uiMieScaleHeight > 0.001f ? uiMieScaleHeight : 0.001f);
			atmosphere.rayleigh_density_exp_scale = -1.0f / (uiRayScaleHeight > 0.001f ? uiRayScaleHeight : 0.001f);
			atmosphere.ground_albedo = { uiGroundAlbedo[0], uiGroundAlbedo[1], uiGroundAlbedo[2] };
			gameGl.setAtmosphereInfo(atmosphere);
			applyAtmosphereUi = false;
		}
		uiMinSpp = gameGl.getRayMarchMinSpp();
		uiMaxSpp = gameGl.getRayMarchMaxSpp();
		gameGl.render();

		ImGui::Begin("Scene");
		ImGui::SliderFloat("Height", &uiCamHeight, 0.001f, 200.0f, "%.3f");
		ImGui::SliderFloat("Forward", &uiCamForward, -20000.0f, -1.0f, "%.3f");
		ImGui::SliderFloat("IllumScale", &uiIllumScale, 0.1f, 100.0f, "%.3f");
		ImGui::SliderFloat("Yaw", &uiSunYaw, -3.14f, 3.14f);
		ImGui::SliderFloat("Pitch", &uiSunPitch, -3.14f, 3.14f);
		ImGui::Checkbox("Animate Sun", &uiAnimateSun);
		ImGui::SliderFloat("Sun Speed", &uiSunAnimSpeed, -2.0f, 2.0f, "%.3f rad/s");
		ImGui::End();

		ImGui::Begin("Debug Camera");
		ImGui::Checkbox("FPS Camera", &uiFpsCamera);
		ImGui::Checkbox("Pointer Lock (F1)", &uiPointerLock);
		ImGui::SliderFloat("Move Speed", &uiCameraMoveSpeed, 1.0f, 400.0f, "%.1f");
		ImGui::SliderFloat("Mouse Sensitivity", &uiMouseSensitivity, 0.0005f, 0.01f, "%.4f");
		ImGui::SliderFloat("View Yaw", &uiViewYaw, -3.14159f, 3.14159f);
		ImGui::SliderFloat("View Pitch", &uiViewPitch, -1.55f, 1.55f);
		ImGui::TextUnformatted("Move: WASD + Q/E, sprint: Shift");
		ImGui::End();

		ImGui::Begin("Atmosphere");
		bool atmosphereEdited = false;
		atmosphereEdited |= ImGui::SliderFloat("Mie phase", &uiMiePhase, 0.0f, 0.999f, "%.3f");
		atmosphereEdited |= ImGui::ColorEdit3("MieScattCoeff", uiMieScattColor);
		atmosphereEdited |= ImGui::SliderFloat("MieScattScale", &uiMieScattScale, 0.00001f, 0.1f, "%.5f");
		atmosphereEdited |= ImGui::ColorEdit3("MieAbsorCoeff", uiMieAbsColor);
		atmosphereEdited |= ImGui::SliderFloat("MieAbsorScale", &uiMieAbsScale, 0.00001f, 10.0f, "%.5f");
		atmosphereEdited |= ImGui::ColorEdit3("RayScattCoeff", uiRayScattColor);
		atmosphereEdited |= ImGui::SliderFloat("RayScattScale", &uiRayScattScale, 0.00001f, 10.0f, "%.5f");
		atmosphereEdited |= ImGui::ColorEdit3("AbsorptiCoeff", uiAbsorpColor);
		atmosphereEdited |= ImGui::SliderFloat("AbsorptiScale", &uiAbsorpScale, 0.00001f, 10.0f, "%.5f");
		atmosphereEdited |= ImGui::SliderFloat("Planet radius", &uiBottomRadius, 100.0f, 8000.0f);
		atmosphereEdited |= ImGui::SliderFloat("Atmos height", &uiAtmosphereHeight, 10.0f, 150.0f);
		atmosphereEdited |= ImGui::SliderFloat("MieScaleHeight", &uiMieScaleHeight, 0.5f, 20.0f);
		atmosphereEdited |= ImGui::SliderFloat("RayScaleHeight", &uiRayScaleHeight, 0.5f, 20.0f);
		atmosphereEdited |= ImGui::ColorEdit3("Ground albedo", uiGroundAlbedo);
		if (atmosphereEdited)
		{
			applyAtmosphereUi = true;
		}
		ImGui::End();

		ImGui::Begin("Render method/Tech");
		ImGui::SliderInt("Min SPP", &uiMinSpp, 1, 30);
		ImGui::SliderInt("Max SPP", &uiMaxSpp, 2, 31);
		ImGui::Checkbox("FastSky", &uiFastSky);
		ImGui::Checkbox("FastAerialPerspective", &uiFastAerialPerspective);
		ImGui::Checkbox("Terrain", &uiRenderTerrain);
		ImGui::SliderFloat("Multi-Scattering approx", &uiMultiScattering, 0.0f, 1.0f);
		ImGui::SliderFloat("AP Debug Depth (km)", &apDebugDepthKm, 0.0f, 128.0f, "%.2f");
		ImGui::End();

		ImGui::Begin("Port Status");
		ImGui::TextUnformatted("OpenGL path active.");
		ImGui::TextUnformatted("Hillaire path stage: Transmittance LUT + Multi-Scattering LUT + SkyView LUT + Aerial Perspective Volume + DX11-like fast branches.");
		ImGui::TextUnformatted("Use SKY_OPENGL_EXPERIMENT=0 to go back to DX11.");
		ImGui::Separator();
		ImGui::Text("Transmittance LUT");
		ImGui::Image((void*)(intptr_t)gameGl.getTransmittanceTexture(), ImVec2(512.0f, 128.0f));
		ImGui::Text("Multi-Scattering LUT");
		ImGui::SliderFloat("MS LUT exposure", &msPreviewExposure, 1.0f, 256.0f, "%.1f");
		ImGui::Image(
			(void*)(intptr_t)gameGl.getMultipleScatteringTexture(),
			ImVec2(256.0f, 256.0f),
			ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f),
			ImVec4(msPreviewExposure, msPreviewExposure, msPreviewExposure, 1.0f));
		if (gameGl.hasMultiScatteringDebugStats())
		{
			ImGui::Text("MS LUT min/max: %.6g / %.6g", gameGl.getMultiScatteringDebugMin(), gameGl.getMultiScatteringDebugMax());
		}
		if (gameGl.hasAerialPerspectiveDebugStats())
		{
			ImGui::Text("AP volume min/max: %.6g / %.6g", gameGl.getAerialPerspectiveDebugMin(), gameGl.getAerialPerspectiveDebugMax());
		}
		ImGui::Text("SkyView LUT");
		ImGui::Image((void*)(intptr_t)gameGl.getSkyViewTexture(), ImVec2(512.0f, 288.0f));
		ImGui::End();

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window);
	}

	gameGl.shutdown();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext(imguiContext);
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}

#endif
