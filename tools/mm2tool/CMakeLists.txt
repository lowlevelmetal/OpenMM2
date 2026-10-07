# Commands live in cmd_*.cpp and register themselves (see Command.h), so new
# format modules can add commands without touching shared files.
file(GLOB mm2tool_commands CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmd_*.cpp")
add_executable(mm2tool main.cpp Common.cpp ${mm2tool_commands})
target_link_libraries(mm2tool PRIVATE OpenMM2::vfs SDL3::SDL3)
foreach(module data asset city phys ai audio game)
    if(TARGET mm2_${module})
        target_link_libraries(mm2tool PRIVATE mm2_${module})
    endif()
endforeach()
