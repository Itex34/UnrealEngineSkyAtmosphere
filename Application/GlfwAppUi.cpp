// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlfwAppUi.h"

#include "GameGl.h"
#include "GlfwAppState.h"

#include <cstdint>

#include <imgui.h>

void drawGlfwUi(GameGl& gameGl, GlfwUiState& state)
{
	ImGui::Begin("Scene");
	ImGui::SliderFloat("Height", &state.uiCamHeight, 0.001f, 200.0f, "%.3f");
	ImGui::SliderFloat("Forward", &state.uiCamForward, -20000.0f, -1.0f, "%.3f");
	ImGui::SliderFloat("IllumScale", &state.uiIllumScale, 0.1f, 100.0f, "%.3f");
	ImGui::SliderFloat("Yaw", &state.uiSunYaw, -3.14f, 3.14f);
	ImGui::SliderFloat("Pitch", &state.uiSunPitch, -3.14f, 3.14f);
	ImGui::Checkbox("Animate Sun", &state.uiAnimateSun);
	ImGui::SliderFloat("Sun Speed", &state.uiSunAnimSpeed, -2.0f, 2.0f, "%.3f rad/s");
	ImGui::End();

	ImGui::Begin("Debug Camera");
	ImGui::Checkbox("FPS Camera", &state.uiFpsCamera);
	ImGui::Checkbox("Pointer Lock (F1)", &state.uiPointerLock);
	ImGui::SliderFloat("Move Speed", &state.uiCameraMoveSpeed, 1.0f, 400.0f, "%.1f");
	ImGui::SliderFloat("Mouse Sensitivity", &state.uiMouseSensitivity, 0.0005f, 0.01f, "%.4f");
	ImGui::SliderFloat("View Yaw", &state.uiViewYaw, -3.14159f, 3.14159f);
	ImGui::SliderFloat("View Pitch", &state.uiViewPitch, -1.55f, 1.55f);
	ImGui::TextUnformatted("Move: WASD + Space/Left ctrl, sprint: Shift");
	ImGui::End();

	ImGui::Begin("Atmosphere");
	bool atmosphereEdited = false;
	atmosphereEdited |= ImGui::SliderFloat("Mie phase", &state.uiMiePhase, 0.0f, 0.999f, "%.3f");
	atmosphereEdited |= ImGui::ColorEdit3("MieScattCoeff", state.uiMieScattColor);
	atmosphereEdited |= ImGui::SliderFloat("MieScattScale", &state.uiMieScattScale, 0.00001f, 0.1f, "%.5f");
	atmosphereEdited |= ImGui::ColorEdit3("MieAbsorCoeff", state.uiMieAbsColor);
	atmosphereEdited |= ImGui::SliderFloat("MieAbsorScale", &state.uiMieAbsScale, 0.00001f, 10.0f, "%.5f");
	atmosphereEdited |= ImGui::ColorEdit3("RayScattCoeff", state.uiRayScattColor);
	atmosphereEdited |= ImGui::SliderFloat("RayScattScale", &state.uiRayScattScale, 0.00001f, 10.0f, "%.5f");
	atmosphereEdited |= ImGui::ColorEdit3("AbsorptiCoeff", state.uiAbsorpColor);
	atmosphereEdited |= ImGui::SliderFloat("AbsorptiScale", &state.uiAbsorpScale, 0.00001f, 10.0f, "%.5f");
	atmosphereEdited |= ImGui::SliderFloat("Planet radius", &state.uiBottomRadius, 100.0f, 8000.0f);
	atmosphereEdited |= ImGui::SliderFloat("Atmos height", &state.uiAtmosphereHeight, 10.0f, 150.0f);
	atmosphereEdited |= ImGui::SliderFloat("MieScaleHeight", &state.uiMieScaleHeight, 0.5f, 20.0f);
	atmosphereEdited |= ImGui::SliderFloat("RayScaleHeight", &state.uiRayScaleHeight, 0.5f, 20.0f);
	atmosphereEdited |= ImGui::ColorEdit3("Ground albedo", state.uiGroundAlbedo);
	if (atmosphereEdited)
	{
		state.applyAtmosphereUi = true;
	}
	ImGui::End();

	ImGui::Begin("Render method/Tech");
	ImGui::SliderInt("Min SPP", &state.uiMinSpp, 1, 30);
	ImGui::SliderInt("Max SPP", &state.uiMaxSpp, 2, 31);
	ImGui::Checkbox("FastSky", &state.uiFastSky);
	ImGui::Checkbox("FastAerialPerspective", &state.uiFastAerialPerspective);
	ImGui::Checkbox("ShadowMap", &state.uiShadowMaps);
	if (state.uiFastAerialPerspective)
	{
		bool disabledColoredTransmittance = state.uiColoredTransmittance;
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
		ImGui::Checkbox("RGB Transmittance", &disabledColoredTransmittance);
		ImGui::PopStyleVar();
	}
	else
	{
		ImGui::Checkbox("RGB Transmittance", &state.uiColoredTransmittance);
	}
	ImGui::Checkbox("Terrain", &state.uiRenderTerrain);
	ImGui::SliderFloat("Multi-Scattering approx", &state.uiMultiScattering, 0.0f, 1.0f);
	ImGui::SliderFloat("AP Debug Depth (km)", &state.apDebugDepthKm, 0.0f, 128.0f, "%.2f");
	ImGui::End();

	ImGui::Begin("Performance");
	if (!gameGl.hasGpuPassTimings())
	{
		ImGui::TextUnformatted("GPU pass timings unavailable on this runtime.");
	}
	else
	{
		const float shadowMs = gameGl.getShadowPassMs();
		const float transMs = gameGl.getTransmittancePassMs();
		const float multiMs = gameGl.getMultiScatteringPassMs();
		const float skyMs = gameGl.getSkyViewPassMs();
		const float apMs = gameGl.getAerialPerspectivePassMs();
		const float terrainMs = gameGl.getTerrainPassMs();
		const float presentMs = gameGl.getPresentPassMs();
		const float totalMs = shadowMs + transMs + multiMs + skyMs + apMs + terrainMs + presentMs;

		ImGui::Text("Shadow map: %.3f ms", shadowMs);
		ImGui::Text("Transmittance LUT: %.3f ms", transMs);
		ImGui::Text("Multi-scattering LUT: %.3f ms", multiMs);
		ImGui::Text("SkyView LUT: %.3f ms", skyMs);
		ImGui::Text("Aerial perspective volume: %.3f ms", apMs);
		ImGui::Text("Terrain scene: %.3f ms", terrainMs);
		ImGui::Text("Present composite: %.3f ms", presentMs);
		ImGui::Separator();
		ImGui::Text("Total (listed passes): %.3f ms", totalMs);
		ImGui::TextUnformatted("GL timer queries update with frame latency.");
	}
	ImGui::End();

	ImGui::Begin("LUT Preview");
	ImGui::TextUnformatted("Transmittance LUT");
	ImGui::Image((void*)(intptr_t)gameGl.getTransmittanceTexture(), ImVec2(512.0f, 128.0f));
	ImGui::TextUnformatted("Multi-Scattering LUT");
	ImGui::SliderFloat("MS LUT exposure", &state.msPreviewExposure, 1.0f, 256.0f, "%.1f");
	ImGui::Image((void*)(intptr_t)gameGl.getMultipleScatteringPreviewTexture(), ImVec2(256.0f, 256.0f));
	ImGui::TextUnformatted("SkyView LUT");
	ImGui::Image((void*)(intptr_t)gameGl.getSkyViewTexture(), ImVec2(512.0f, 288.0f));
	ImGui::TextUnformatted("Aerial Perspective LUT (slice)");
	const int apSliceCount = gameGl.getAerialPerspectiveDepthSliceCount();
	const int apSliceMax = (apSliceCount > 0) ? (apSliceCount - 1) : 0;
	ImGui::SliderInt("AP Slice", &state.apPreviewSlice, 0, apSliceMax);
	ImGui::SliderFloat("AP LUT exposure", &state.apPreviewExposure, 1.0f, 256.0f, "%.1f");
	ImGui::Image((void*)(intptr_t)gameGl.getAerialPerspectivePreviewTexture(), ImVec2(256.0f, 256.0f));
	ImGui::End();
}
