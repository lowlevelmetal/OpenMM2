file(GLOB phys_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/vehicle/*.cpp")
add_library(mm2_phys STATIC ${phys_sources})
target_link_libraries(mm2_phys PUBLIC OpenMM2::core OpenMM2::data)
# The simulation ports x87 single-precision code operation by operation;
# keep the compiler from fusing or reassociating float operations so results
# are deterministic across builds and platforms.
if(MSVC)
    target_compile_options(mm2_phys PRIVATE /fp:precise)
else()
    target_compile_options(mm2_phys PRIVATE -ffp-contract=off -fno-fast-math)
endif()
add_library(OpenMM2::phys ALIAS mm2_phys)
