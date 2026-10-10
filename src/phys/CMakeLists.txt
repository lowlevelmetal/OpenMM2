file(GLOB phys_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vehicle/*.cpp")
add_library(mm2_phys STATIC ${phys_sources})
target_link_libraries(mm2_phys PUBLIC OpenMM2::core OpenMM2::data)
# The simulation ports x87 single-precision code operation by operation; the
# whole project is built without contraction or fast-math (OpenMM2::options,
# cmake/CompilerOptions.cmake), so it rounds alike on every platform.
add_library(OpenMM2::phys ALIAS mm2_phys)
