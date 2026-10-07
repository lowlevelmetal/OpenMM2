if(TARGET mm2_render)
    add_executable(rendertest main.cpp)
    target_link_libraries(rendertest PRIVATE OpenMM2::render)
    if(MINGW)
        # The project builds with UNICODE, so SDL_main provides wmain/wWinMain;
        # MinGW only uses those entry points with -municode.
        target_link_options(rendertest PRIVATE -municode)
    endif()
endif()
