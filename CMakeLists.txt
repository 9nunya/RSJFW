cmake_minimum_required(VERSION 3.20)
project(RSJFW VERSION 1.0.0 LANGUAGES CXX)

# Auto-initialize submodules
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    RESULT_VARIABLE GIT_SUBMOD_RESULT)
    if(NOT GIT_SUBMOD_RESULT EQUAL "0")
        message(FATAL_ERROR "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED libzip)
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/imgui
    ${PROJECT_SOURCE_DIR}/external/imgui/backends
    ${PROJECT_SOURCE_DIR}/external/stb
    ${CURL_INCLUDE_DIRS}
    ${LIBZIP_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
)
# Bundle nlohmann json
include_directories(include/nlohmann)

# Sources
file(GLOB_RECURSE SOURCES "src/*.cpp")
# Exclude layer sources from main executable
list(FILTER SOURCES EXCLUDE REGEX "src/layer/.*")

# ImGui sources for GLFW/OpenGL3
set(IMGUI_SOURCES
    external/imgui/imgui.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)

# Executable
add_executable(rsjfw ${SOURCES} ${IMGUI_SOURCES})
target_link_libraries(rsjfw PRIVATE 
    glfw
    OpenGL::GL
    ${LIBZIP_LIBRARIES}
    CURL::libcurl
    X11::X11
)

# Vulkan Layer Library
add_library(VkLayer_RSJFW_RsjfwLayer SHARED src/layer/rsjfw_layer.cpp)
target_include_directories(VkLayer_RSJFW_RsjfwLayer PRIVATE src/layer)
target_link_libraries(VkLayer_RSJFW_RsjfwLayer PRIVATE Vulkan::Vulkan)

# Installation
install(TARGETS rsjfw DESTINATION bin)
install(TARGETS VkLayer_RSJFW_RsjfwLayer DESTINATION lib) # .so goes to lib
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/layer/VkLayer_RSJFW_RsjfwLayer.json DESTINATION share/vulkan/implicit_layer.d)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/rsjfw.desktop DESTINATION share/applications)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo.png DESTINATION share/pixmaps RENAME rsjfw.png)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE DESTINATION share/licenses/rsjfw)

# Developer convenience: Update desktop file in local user dir on build
add_custom_command(TARGET rsjfw POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $ENV{HOME}/.local/share/applications
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/rsjfw.desktop $ENV{HOME}/.local/share/applications/rsjfw.desktop
    COMMENT "Installing desktop file to ~/.local/share/applications"
)

# CPack Configuration
set(CPACK_PACKAGE_NAME "rsjfw")
set(CPACK_PACKAGE_VENDOR "RSJFW Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Roblox Studio Just Fucking Works - Linux Compatibility Layer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4, libzip4, wine, wine64")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "User <user@example.com>")

# Determine architecture for DEB
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
endif()

include(CPack)
