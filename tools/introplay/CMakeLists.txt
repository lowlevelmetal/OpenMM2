# Intro movie checker/player. IntroScreen.cpp is compiled in directly so the
# tool works before the app target lists it.
add_executable(introplay main.cpp "${PROJECT_SOURCE_DIR}/src/app/IntroScreen.cpp")
target_link_libraries(introplay PRIVATE mm2_app mm2_video SDL3::SDL3)
if(MINGW)
    target_link_options(introplay PRIVATE -municode)
endif()
