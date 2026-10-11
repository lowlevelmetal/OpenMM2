# Third-party dependencies.
#
# Every dependency is pinned to an upstream release tag. When
# OPENMM2_USE_SYSTEM_DEPS is ON, an installed package is preferred where one is
# commonly available (Linux distributions); otherwise the source is fetched and
# built as part of the project, which is what Windows builds do.

include(FetchContent)

set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
if(OPENMM2_USE_SYSTEM_DEPS)
    set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE OPT_IN)
else()
    set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
endif()

# Let our set() calls below override option() defaults in fetched projects.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# --- SDL3: windowing, input, audio device, file dialogs ----------------------
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TEST_LIBRARY OFF)
set(SDL_EXAMPLES OFF)
set(SDL_TESTS OFF)
set(SDL_INSTALL OFF)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE
    SYSTEM
    FIND_PACKAGE_ARGS 3.2 CONFIG)

# --- miniz: raw deflate decoding for DAVE archives ---------------------------
# Built from its sources directly; we only need the inflater and its upstream
# CMake setup is geared toward standalone use.
FetchContent_Declare(miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.1.2
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)

# --- ENet: reliable UDP transport for multiplayer ----------------------------
FetchContent_Declare(enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.18
    GIT_SHALLOW TRUE
    SYSTEM)

# --- miniupnpc: UPnP IGD automatic port forwarding ---------------------------
set(UPNPC_BUILD_STATIC ON)
set(UPNPC_BUILD_SHARED OFF)
set(UPNPC_BUILD_TESTS OFF)
set(UPNPC_BUILD_SAMPLE OFF)
set(UPNPC_NO_INSTALL ON)
FetchContent_Declare(miniupnpc
    GIT_REPOSITORY https://github.com/miniupnp/miniupnp.git
    GIT_TAG miniupnpc_2_3_3
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR miniupnpc
    SYSTEM)

# --- Dear ImGui: setup wizard, debug tooling ---------------------------------
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)

FetchContent_MakeAvailable(SDL3 miniz enet miniupnpc imgui)

# miniz ---------------------------------------------------------------------
set(_miniz_gen "${miniz_BINARY_DIR}/generated")
file(WRITE "${_miniz_gen}/miniz_export.h" "#pragma once\n#define MINIZ_EXPORT\n")
add_library(openmm2_miniz STATIC
    "${miniz_SOURCE_DIR}/miniz.c"
    "${miniz_SOURCE_DIR}/miniz_tdef.c"
    "${miniz_SOURCE_DIR}/miniz_tinfl.c"
    "${miniz_SOURCE_DIR}/miniz_zip.c")
target_include_directories(openmm2_miniz SYSTEM PUBLIC "${miniz_SOURCE_DIR}" "${_miniz_gen}")
target_compile_definitions(openmm2_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_target_properties(openmm2_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(OpenMM2::miniz ALIAS openmm2_miniz)

# ENet: upstream does not export its include directory or Windows libs for MSVC.
target_include_directories(enet SYSTEM PUBLIC "${enet_SOURCE_DIR}/include")
if(WIN32 AND NOT MINGW)
    target_link_libraries(enet PUBLIC winmm ws2_32)
endif()
add_library(OpenMM2::enet ALIAS enet)

# Dear ImGui with the SDL3 platform backend; renderer backends are added by the
# renderer libraries that need them.
add_library(openmm2_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp")
target_include_directories(openmm2_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_link_libraries(openmm2_imgui PUBLIC SDL3::SDL3)
target_compile_definitions(openmm2_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
add_library(OpenMM2::imgui ALIAS openmm2_imgui)

# --- Vulkan: headers + volk loader + VMA (no link-time dependency on a loader)
if(OPENMM2_ENABLE_VULKAN)
    FetchContent_Declare(VulkanHeaders
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG vulkan-sdk-1.4.363.0
        GIT_SHALLOW TRUE
        SYSTEM)
    set(VOLK_PULL_IN_VULKAN OFF)
    FetchContent_Declare(volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG vulkan-sdk-1.4.363.0
        GIT_SHALLOW TRUE
        SYSTEM)
    set(VMA_ENABLE_INSTALL OFF)
    FetchContent_Declare(VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE
        SYSTEM)
    FetchContent_MakeAvailable(VulkanHeaders volk VulkanMemoryAllocator)
    target_link_libraries(volk PUBLIC Vulkan::Headers)
    target_link_libraries(VulkanMemoryAllocator INTERFACE Vulkan::Headers)
endif()

# --- stb: header-only image decoders (JPEG for jpg/*.jpg UI art) -------------
# Pinned to a master commit (stb has no release tags). Consumers compile the
# implementation themselves (STB_IMAGE_IMPLEMENTATION in one source file).
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
    SOURCE_SUBDIR _no_cmake)
FetchContent_MakeAvailable(stb)
add_library(openmm2_stb INTERFACE)
target_include_directories(openmm2_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
add_library(OpenMM2::stb ALIAS openmm2_stb)

# --- dmusic: DirectMusic style/segment playback for the soundtrack -----------
# GothicKit/dmusic (MIT), pinned to a master commit. Built from its sources as
# our own static target: upstream's CMake forces -Werror and sanitizers.
FetchContent_Declare(dmusic
    URL https://github.com/GothicKit/dmusic/archive/b73b270b94714b8ec8d9d0b2a9f3a5c445da52d1.tar.gz
    URL_HASH SHA256=ac91c0f16075f929ee2b636cafb20c909c6af706bd03241f5775d379ae3cd6c5
    SOURCE_SUBDIR _no_cmake)
FetchContent_MakeAvailable(dmusic)
set(_dm "${dmusic_SOURCE_DIR}")
# Fixes to upstream sources, compiled from patched copies. Each replacement is
# checked so a pin bump notices when it no longer applies.
#  Band.c 1: a band instrument whose collection cannot be found (the system
#     gm.dls some MM2 bands reference) only silences that instrument instead of
#     failing the whole band and with it the segment.
#  Band.c 2: instrument lookup underflowed `instrument_count - 1` (uint32_t)
#     for an empty collection.
#  Performance.c 1: a style part without valid variations (MM2's Pause.sty)
#     caused a division by zero; such parts are skipped.
#  Performance.c 2: DmPattern_generateMessages is made non-static for
#     src/audio/MusicMotif.c (motif playback), which is built into this library.
#  Performance.c 3: a segment whose style track starts after the segment
#     start (MM2's DuckCops.sgt starts its style in measure 5 but commands a
#     pattern at 0) uses that first style from the start instead of
#     dereferencing a NULL style; command messages without any style are ignored.
#  Performance.c 4: when a segment starts after tick 0 (play/loop start; MM2's
#     "Return"/"Restart" segments resume mid-song), the control state before
#     the start point (style, band, tempo, chord, groove command) takes effect
#     at the start instead of being discarded; notes before it are still cut.
function(openmm2_dmusic_patch file out)
    file(READ "${_dm}/src/${file}" text)
    set(i 0)
    list(LENGTH ARGN n)
    while(i LESS n)
        list(GET ARGN ${i} from)
        math(EXPR j "${i} + 1")
        list(GET ARGN ${j} to)
        string(FIND "${text}" "${from}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "dmusic: src/${file} changed upstream; update the OpenMM2 patch in cmake/Dependencies.cmake")
        endif()
        string(REPLACE "${from}" "${to}" text "${text}")
        math(EXPR i "${i} + 2")
    endwhile()
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/dmusic-patched/${file}" CONTENT "${text}" @ONLY)
    set(${out} "${CMAKE_BINARY_DIR}/dmusic-patched/${file}" PARENT_SCOPE)
endfunction()
openmm2_dmusic_patch(Band.c _dm_band
    "rv = DmLoader_getDownloadableSound(loader, &instrument->reference, &instrument->dls)\;\n\t\tif (rv != DmResult_SUCCESS || instrument->dls == NULL) {"
    "rv = DmLoader_getDownloadableSound(loader, &instrument->reference, &instrument->dls)\;\n\t\tif (rv == DmResult_NOT_FOUND) {\n\t\t\trv = DmResult_SUCCESS\; /* OpenMM2: missing collection -> silent instrument */\n\t\t\tcontinue\;\n\t\t}\n\t\tif (rv != DmResult_SUCCESS || instrument->dls == NULL) {"
    "for (long long i = slf->dls->instrument_count-1\; i >= 0\; --i)"
    "for (long long i = (long long) slf->dls->instrument_count - 1\; i >= 0\; --i)")
openmm2_dmusic_patch(Performance.c _dm_performance
    "static DmResult DmPattern_generateMessages(DmPattern* slf,"
    "DmResult DmPattern_generateMessages(DmPattern* slf,"
    "variation_id = 1 << (variation_id % DmPart_getValidVariationCount(part))\;"
    "uint32_t variation_count = DmPart_getValidVariationCount(part)\;\n\t\tif (variation_count == 0) {\n\t\t\tcontinue\; /* OpenMM2: part without valid variations */\n\t\t}\n\t\tvariation_id = 1 << (variation_id % variation_count)\;"
    "DmPattern* pttn = DmStyle_getRandomPattern(slf->style, slf->groove, msg->command)\;"
    "if (slf->style == NULL) {\n\t\treturn\; /* OpenMM2: no style yet */\n\t}\n\tDmPattern* pttn = DmStyle_getRandomPattern(slf->style, slf->groove, msg->command)\;"
    "\t\tif (m->time < start || m->time > end) {\n\t\t\tcontinue\;\n\t\t}"
    "\t\tif (m->time > end) {\n\t\t\tcontinue\;\n\t\t}\n\t\tif (m->time < start) {\n\t\t\tif (m->type != DmMessage_NOTE) {\n\t\t\t\tDmMessageQueue_add(&slf->control_queue, m, slf->time, DmQueueConflict_REPLACE)\; /* OpenMM2: state at the start point */\n\t\t\t}\n\t\t\tcontinue\;\n\t\t}"
    "\t// If we don't yet have a command, add it!"
    "\t/* OpenMM2: apply a style track's first style from the segment start. */\n\t{\n\t\tDmMessage* first_style = NULL\;\n\t\tfor (size_t i = 0\; i < sgt->messages.length\; ++i) {\n\t\t\tDmMessage* m = &sgt->messages.data[i]\;\n\t\t\tif (m->type == DmMessage_STYLE && (first_style == NULL || m->time < first_style->time)) {\n\t\t\t\tfirst_style = m\;\n\t\t\t}\n\t\t}\n\t\tif (first_style != NULL && first_style->time != 0xffffffff && first_style->time > start) {\n\t\t\tDmMessageQueue_add(&slf->control_queue, first_style, slf->time, DmQueueConflict_REPLACE)\;\n\t\t}\n\t}\n\n\t// If we don't yet have a command, add it!")
add_library(openmm2_dmusic STATIC
    "${_dm}/vendor/TinySoundFont/tsf.c"
    "${_dm}/src/util/Tsf.c"
    "${_dm}/src/io/Band.c"
    "${_dm}/src/io/Common.c"
    "${_dm}/src/io/Dls.c"
    "${_dm}/src/io/Segment.c"
    "${_dm}/src/io/Style.c"
    "${_dm}/src/Array.c"
    "${_dm}/src/Common.c"
    "${_dm}/src/Composer.c"
    "${_dm}/src/Dls.c"
    "${_dm_band}"
    "${_dm}/src/Loader.c"
    "${_dm}/src/Logger.c"
    "${_dm}/src/Memory.c"
    "${_dm}/src/Message.c"
    "${_dm_performance}"
    "${PROJECT_SOURCE_DIR}/src/audio/MusicMotif.c"
    "${_dm}/src/Riff.c"
    "${_dm}/src/Rng.c"
    "${_dm}/src/Segment.c"
    "${_dm}/src/Style.c"
    "${_dm}/src/Synth.c")
target_include_directories(openmm2_dmusic SYSTEM PUBLIC "${_dm}/include")
target_include_directories(openmm2_dmusic PRIVATE "${_dm}/src" "${_dm}/vendor/TinySoundFont")
target_compile_definitions(openmm2_dmusic PUBLIC DM_STATIC=1 PRIVATE DM_BUILD=1)
set_target_properties(openmm2_dmusic PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
include(CheckCSourceCompiles)
check_c_source_compiles("
    #include <threads.h>
    int main(void) { mtx_t m; if (mtx_init(&m, mtx_recursive) != thrd_success) return 1; mtx_destroy(&m); return 0; }"
    OPENMM2_HAVE_C11_THREADS)
# Not on MSVC: its C11 threads live in vcruntime140_threads.dll (VS 2022 17.8+),
# which older VC++ redistributables lack and the packages did not ship, so the
# game failed to start on such PCs. The library's Win32 threads, as on MinGW.
if(OPENMM2_HAVE_C11_THREADS AND NOT MSVC)
    target_compile_definitions(openmm2_dmusic PRIVATE _DM_USE_NATIVE_THREAD=1)
else()
    target_sources(openmm2_dmusic PRIVATE "${_dm}/src/thread/Thread.Posix.c" "${_dm}/src/thread/Thread.Win32.c")
endif()
if(MSVC)
    target_compile_options(openmm2_dmusic PRIVATE /experimental:c11atomics /w)
else()
    target_compile_options(openmm2_dmusic PRIVATE -w)
endif()
if(MINGW)
    target_compile_definitions(openmm2_dmusic PRIVATE _POSIX_C_SOURCE=1)
endif()
find_package(Threads REQUIRED)
target_link_libraries(openmm2_dmusic PRIVATE Threads::Threads)
if(NOT WIN32)
    target_link_libraries(openmm2_dmusic PRIVATE m)
endif()
add_library(OpenMM2::dmusic ALIAS openmm2_dmusic)

# --- Tests -------------------------------------------------------------------
if(OPENMM2_BUILD_TESTS)
    set(gtest_force_shared_crt ON)
    set(INSTALL_GTEST OFF)
    set(BUILD_GMOCK OFF)
    FetchContent_Declare(GTest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.18.0
        GIT_SHALLOW TRUE
        SYSTEM
        FIND_PACKAGE_ARGS 1.14 NAMES GTest)
    FetchContent_MakeAvailable(GTest)
    if(NOT TARGET GTest::gtest_main AND TARGET gtest_main)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
endif()

find_package(Threads REQUIRED)
