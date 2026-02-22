// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameGl.h"
#include "GlfwAppLoop.h"

#include <cstdio>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/gl3w.h>

#include <imgui.h>
#include "imgui/examples/imgui_impl_glfw.h"
#include "imgui/examples/imgui_impl_opengl3.h"

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	if (!glfwInit())
	{
		std::fprintf(stderr, "glfwInit() failed.\n");
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
		std::fprintf(stderr, "glfwCreateWindow() failed.\n");
		return -1;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	if (gl3wInit() != 0)
	{
		glfwDestroyWindow(window);
		glfwTerminate();
		std::fprintf(stderr, "gl3wInit() failed.\n");
		return -1;
	}
	if (!gl3wIsSupported(4, 3))
	{
		glfwDestroyWindow(window);
		glfwTerminate();
		std::fprintf(stderr, "OpenGL 4.3 is not available.\n");
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
		std::fprintf(stderr, "ImGui backend initialization failed.\n");
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
