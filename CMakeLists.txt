cmake_minimum_required(VERSION 3.20)
project(RSJFW VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED libzip)

# Include directories
include_directories(include)
include_directories(external)
include_directories(${CURL_INCLUDE_DIRS})
include_directories(${LIBZIP_INCLUDE_DIRS})
# Bundle nlohmann json
include_directories(include/nlohmann)

# Sources
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Executable
add_executable(rsjfw ${SOURCES})
target_link_libraries(rsjfw PRIVATE 
    ${CURL_LIBRARIES}
    ${LIBZIP_LIBRARIES}
    pthread
)

# Installation
install(TARGETS rsjfw DESTINATION bin)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/rsjfw.desktop DESTINATION share/applications)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/logo.png DESTINATION share/pixmaps RENAME rsjfw.png)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE DESTINATION share/licenses/rsjfw)

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
