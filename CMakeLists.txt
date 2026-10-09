cmake_minimum_required(VERSION 3.25...4.0)

project(OpenMM2
    VERSION 0.3.1
    DESCRIPTION "Open source reimplementation of Midtown Madness 2"
    HOMEPAGE_URL "https://github.com/lowlevelmetal/OpenMM2"
    LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a build directory or a preset.")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(OPENMM2_BUILD_TESTS "Build unit tests" ON)
option(OPENMM2_BUILD_TOOLS "Build developer tools (mm2tool)" ON)
option(OPENMM2_ENABLE_VULKAN "Build the Vulkan renderer" ON)
option(OPENMM2_ENABLE_OPENGL "Build the OpenGL fallback renderer" ON)
option(OPENMM2_USE_SYSTEM_DEPS "Prefer system packages over fetching dependencies" ON)
option(OPENMM2_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(NOT OPENMM2_ENABLE_VULKAN AND NOT OPENMM2_ENABLE_OPENGL)
    message(FATAL_ERROR "At least one renderer (Vulkan or OpenGL) must be enabled.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Keep all runtime artifacts together so the game, tools and tests can find
# each other (and shared libraries) without installation.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

include(CompilerOptions)
include(Dependencies)
include(Shaders)
include(Fonts)

# Source modules in dependency order; see src/CMakeLists.txt.
set(OPENMM2_MODULES core vfs data asset city platform render audio video phys ai net ui game app)

add_subdirectory(src)

if(OPENMM2_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(OPENMM2_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(Packaging)
