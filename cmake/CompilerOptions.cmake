# Project-wide compiler configuration, applied through an interface target so
# third-party code fetched with FetchContent is not affected.

add_library(openmm2_options INTERFACE)
add_library(OpenMM2::options ALIAS openmm2_options)

target_compile_features(openmm2_options INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(openmm2_options INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc
        /wd4324) # structure padded due to alignment specifier
    if(OPENMM2_WARNINGS_AS_ERRORS)
        target_compile_options(openmm2_options INTERFACE /WX)
    endif()
    target_compile_definitions(openmm2_options INTERFACE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    target_compile_options(openmm2_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align
        -Woverloaded-virtual -Wimplicit-fallthrough -Wno-missing-field-initializers)
    if(OPENMM2_WARNINGS_AS_ERRORS)
        target_compile_options(openmm2_options INTERFACE -Werror)
    endif()
    if(WIN32)
        target_compile_definitions(openmm2_options INTERFACE
            NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    endif()
endif()

if(WIN32)
    # Windows 7 is the oldest target; SDL3 and Vulkan loaders still run there.
    target_compile_definitions(openmm2_options INTERFACE _WIN32_WINNT=0x0601)
endif()

if(MINGW)
    # Ship a self-contained executable: no libstdc++/libgcc/winpthread DLLs.
    target_link_options(openmm2_options INTERFACE -static -static-libgcc -static-libstdc++)
endif()

if(MINGW AND NOT CMAKE_CXX_STANDARD_LIBRARIES MATCHES "stdc\\+\\+exp")
    # libstdc++ keeps the Windows console backend of std::print in libstdc++exp
    # (GCC 14+). It has to come after every object and static library, which
    # the standard-libraries variable guarantees. The cross toolchain file sets
    # this already; this covers native MSYS2 builds.
    string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -lstdc++exp")
endif()
