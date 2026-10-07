# One test executable per module (tests/<module>/test_*.cpp) so a module can be
# tested in isolation. Tests needing retail game data read the location from
# OPENMM2_GAME_DATA (a disc image, disc mount or install directory) and call
# GTEST_SKIP() when it is unset.
include(GoogleTest)

foreach(module IN LISTS OPENMM2_MODULES)
    set(dir "${CMAKE_CURRENT_SOURCE_DIR}/${module}")
    if(NOT IS_DIRECTORY "${dir}" OR NOT TARGET mm2_${module})
        continue()
    endif()
    file(GLOB sources CONFIGURE_DEPENDS "${dir}/test_*.cpp")
    if(NOT sources)
        continue()
    endif()
    add_executable(test_${module} ${sources} "${CMAKE_CURRENT_SOURCE_DIR}/TestData.cpp")
    target_include_directories(test_${module} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_${module} PRIVATE mm2_${module} OpenMM2::vfs GTest::gtest_main)
    gtest_discover_tests(test_${module} DISCOVERY_MODE PRE_TEST)
endforeach()
