# Intro movie playback: AVI demuxing and an Indeo 5 decoder translated from
# FFmpeg (LGPL-2.1-or-later, used here under GPL-3.0-or-later; see docs/video.md).
add_library(mm2_video STATIC
    Avi.cpp
    Indeo5.cpp
    Movie.cpp)
target_link_libraries(mm2_video PUBLIC OpenMM2::core OpenMM2::audio)
add_library(OpenMM2::video ALIAS mm2_video)
