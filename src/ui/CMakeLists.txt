# Sources: every *.cpp in this directory.
file(GLOB ui_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(mm2_ui STATIC ${ui_sources})
target_link_libraries(mm2_ui
    PUBLIC OpenMM2::core OpenMM2::render OpenMM2::vfs OpenMM2::asset OpenMM2::platform
    PRIVATE OpenMM2::stb)
add_library(OpenMM2::ui ALIAS mm2_ui)
