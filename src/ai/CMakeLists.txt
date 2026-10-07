# Sources: every *.cpp in this directory.
file(GLOB ai_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(mm2_ai STATIC ${ai_sources})
target_link_libraries(mm2_ai PUBLIC OpenMM2::core OpenMM2::data OpenMM2::city OpenMM2::asset OpenMM2::vfs)
add_library(OpenMM2::ai ALIAS mm2_ai)
