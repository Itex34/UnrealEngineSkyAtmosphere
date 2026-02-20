// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuildConfig.h"

#if SKY_OPENGL_EXPERIMENT

#include "GameGl.h"
#include "GlfwAppLoop.h"

#include <windows.h>

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

	GLFWwindow* window = glfwCreateWindow(1280, 720, "Sky Atmosphere - OpenGL", nullptr, nullptr);
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
	if (!ImGui_ImplGlfw_InitForOpenGL(window, true) || !ImGui_ImplOpenGL3_Init("#version 430"))
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext(imguiContext);
		glfwDestroyWindow(window);
		glfwTerminate();
		MessageBoxA(nullptr, "ImGui OpenGL backend initialization failed.", "OpenGL bootstrap error", MB_ICONERROR | MB_OK);
		return -1;
	}

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

	runGlfwMainLoop(window, gameGl);

	gameGl.shutdown();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext(imguiContext);
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}

#endif
