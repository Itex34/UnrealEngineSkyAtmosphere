cmake_minimum_required(VERSION 3.21)

project(HillaireSkyAtmosphereOpenGL LANGUAGES C CXX)

option(SKYATMOS_FETCH_DEPS "Fetch GLFW and GLM via FetchContent when not found locally." ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(SKYATMOS_FETCH_DEPS)
	find_package(glfw3 CONFIG QUIET)
	find_package(glm CONFIG QUIET)

	include(FetchContent)

	if(NOT TARGET glfw AND NOT TARGET glfw3 AND NOT TARGET glfw3::glfw)
		set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
		set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
		set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
		set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
		FetchContent_Declare(
			glfw
			GIT_REPOSITORY https://github.com/glfw/glfw.git
			GIT_TAG 3.4
		)
		FetchContent_MakeAvailable(glfw)
	endif()

	if(NOT TARGET glm::glm AND NOT TARGET glm)
		FetchContent_Declare(
			glm
			GIT_REPOSITORY https://github.com/g-truc/glm.git
			GIT_TAG 1.0.1
		)
		FetchContent_MakeAvailable(glm)
	endif()
else()
	find_package(glfw3 CONFIG REQUIRED)
	find_package(glm CONFIG REQUIRED)
endif()

if(TARGET glfw)
	set(SKYATMOS_GLFW_TARGET glfw)
elseif(TARGET glfw3)
	set(SKYATMOS_GLFW_TARGET glfw3)
elseif(TARGET glfw3::glfw)
	set(SKYATMOS_GLFW_TARGET glfw3::glfw)
else()
	message(FATAL_ERROR "GLFW target not found. Install glfw3 or enable SKYATMOS_FETCH_DEPS.")
endif()

if(TARGET glm::glm)
	set(SKYATMOS_GLM_TARGET glm::glm)
elseif(TARGET glm)
	set(SKYATMOS_GLM_TARGET glm)
else()
	message(FATAL_ERROR "GLM target not found. Install GLM or enable SKYATMOS_FETCH_DEPS.")
endif()

find_package(OpenGL REQUIRED)

set(SKYATMOS_SHADER_SOURCES
	Resources/glsl/fullscreen_triangle.vert
	Resources/glsl/transmittance_lut.frag
	Resources/glsl/new_multi_scattering_lut.comp
	Resources/glsl/skyview_lut.frag
	Resources/glsl/aerial_perspective_volume.comp
	Resources/glsl/terrain.vert
	Resources/glsl/terrain.frag
	Resources/glsl/terrain_shadow.frag
	Resources/glsl/render_raymarching_hillaire.frag
	Resources/glsl/postprocess.frag
)

add_executable(HillaireSkyAtmosphereOpenGL
	Application/main.cpp
	Application/GameGl.cpp
	Application/GlfwAppLoop.cpp
	Application/GlfwAppUi.cpp
	Application/TinyExrImpl.cpp
	imgui/imgui.cpp
	imgui/imgui_demo.cpp
	imgui/imgui_draw.cpp
	imgui/imgui_widgets.cpp
	imgui/examples/imgui_impl_glfw.cpp
	imgui/examples/imgui_impl_opengl3.cpp
	imgui/examples/libs/gl3w/GL/gl3w.c
	${SKYATMOS_SHADER_SOURCES}
)

target_include_directories(HillaireSkyAtmosphereOpenGL PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/imgui
	${CMAKE_CURRENT_SOURCE_DIR}/imgui/examples
	${CMAKE_CURRENT_SOURCE_DIR}/imgui/examples/libs/gl3w
)

target_compile_definitions(HillaireSkyAtmosphereOpenGL PRIVATE
	IMGUI_IMPL_OPENGL_LOADER_GL3W
	IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)

if(WIN32)
	target_compile_definitions(HillaireSkyAtmosphereOpenGL PRIVATE NOMINMAX)
endif()

if(MSVC)
	target_compile_options(HillaireSkyAtmosphereOpenGL PRIVATE /W4 /EHsc)
else()
	target_compile_options(HillaireSkyAtmosphereOpenGL PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(HillaireSkyAtmosphereOpenGL PRIVATE
	${SKYATMOS_GLFW_TARGET}
	${SKYATMOS_GLM_TARGET}
	OpenGL::GL
)

# Keep runtime assets next to the executable so working directory does not depend on IDE settings.
add_custom_command(TARGET HillaireSkyAtmosphereOpenGL POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		${CMAKE_SOURCE_DIR}/Resources
		$<TARGET_FILE_DIR:HillaireSkyAtmosphereOpenGL>/Resources
)

source_group("Shaders" FILES ${SKYATMOS_SHADER_SOURCES})

set_target_properties(HillaireSkyAtmosphereOpenGL PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT HillaireSkyAtmosphereOpenGL)
