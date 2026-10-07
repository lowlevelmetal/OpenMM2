# Every tools/<name>/ with a CMakeLists.txt is a developer tool.
file(GLOB tool_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(dir IN LISTS tool_dirs)
    if(IS_DIRECTORY "${dir}" AND EXISTS "${dir}/CMakeLists.txt")
        add_subdirectory("${dir}")
    endif()
endforeach()
