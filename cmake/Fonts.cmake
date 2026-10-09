# Open fonts standing in for the fonts the original game asked Windows for
# (see docs/formats/strings.md). They are used only when the original font is
# not installed on the system:
#   Arial Bold   -> Liberation Sans Bold (SIL OFL 1.1, metric-compatible)
#   Gill Sans MT -> Gillius ADF No2 (GPL 2+ with font exception)
#
# Downloads are verified by hash and tried from several mirrors. A failed
# download is not fatal: the game then needs the original fonts installed.

set(OPENMM2_FONT_CACHE "${CMAKE_BINARY_DIR}/_fonts" CACHE PATH "Download cache for bundled fonts")

# openmm2_fetch_archive(<name> <sha256> <url>...) -> sets <name>_DIR on success
function(openmm2_fetch_archive name sha256)
    set(dir "${OPENMM2_FONT_CACHE}/${name}")
    if(EXISTS "${dir}/.complete")
        set(${name}_DIR "${dir}" PARENT_SCOPE)
        return()
    endif()
    set(archive "${OPENMM2_FONT_CACHE}/${name}.download")
    foreach(url IN LISTS ARGN)
        # The hash is checked after the download: with EXPECTED_HASH a failed
        # download is a CMake error ("cannot compute hash on failed download"),
        # which ends the configure before the other mirrors are tried.
        file(DOWNLOAD "${url}" "${archive}" STATUS status TIMEOUT 120 INACTIVITY_TIMEOUT 30)
        list(GET status 0 code)
        if(code EQUAL 0)
            file(SHA256 "${archive}" actual)
            if(actual STREQUAL sha256)
                file(REMOVE_RECURSE "${dir}")
                file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${dir}")
                file(REMOVE "${archive}")
                file(TOUCH "${dir}/.complete")
                set(${name}_DIR "${dir}" PARENT_SCOPE)
                return()
            endif()
            set(status "SHA256 ${actual}, expected ${sha256}")
        endif()
        message(STATUS "Font download failed from ${url}: ${status}")
    endforeach()
    file(REMOVE "${archive}")
    message(WARNING "Could not download ${name}; the game will need the original fonts installed.")
endfunction()

openmm2_fetch_archive(liberation 7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0
    https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz)
openmm2_fetch_archive(gillius 9be6357daf8f00126f51235b36a32d9e5ee493d4f6d650d42ba9959b77e2a354
    https://ctan.math.illinois.edu/fonts/gillius.zip
    https://mirrors.rit.edu/CTAN/fonts/gillius.zip
    https://mirrors.ctan.org/fonts/gillius.zip)

set(OPENMM2_FONT_FILES)
if(liberation_DIR)
    set(_lib "${liberation_DIR}/liberation-fonts-ttf-2.1.5")
    list(APPEND OPENMM2_FONT_FILES
        "${_lib}/LiberationSans-Regular.ttf=LiberationSans-Regular.ttf"
        "${_lib}/LiberationSans-Bold.ttf=LiberationSans-Bold.ttf"
        "${_lib}/LICENSE=LiberationSans-LICENSE.txt")
endif()
if(gillius_DIR)
    set(_gil "${gillius_DIR}/gillius")
    list(APPEND OPENMM2_FONT_FILES
        "${_gil}/opentype/GilliusADFNo2-Regular.otf=GilliusADFNo2-Regular.otf"
        "${_gil}/opentype/GilliusADFNo2-Bold.otf=GilliusADFNo2-Bold.otf"
        "${_gil}/doc/COPYING=GilliusADF-COPYING.txt")
endif()

# Fonts live in <bin>/fonts next to the executables, in the build tree and
# when installed (see cmake/Packaging.cmake).
# Multi-config generators (Visual Studio, Ninja Multi-Config) put executables
# in <bin>/<config>/, so the fonts are copied there too.
set(OPENMM2_FONT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/fonts")
set(_font_dirs "${OPENMM2_FONT_DIR}")
foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    list(APPEND _font_dirs "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${config}/fonts")
endforeach()
foreach(dir IN LISTS _font_dirs)
    file(MAKE_DIRECTORY "${dir}")
    foreach(entry IN LISTS OPENMM2_FONT_FILES)
        string(REPLACE "=" ";" pair "${entry}")
        list(GET pair 0 src)
        list(GET pair 1 dst)
        configure_file("${src}" "${dir}/${dst}" COPYONLY)
    endforeach()
endforeach()
