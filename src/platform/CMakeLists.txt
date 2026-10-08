add_library(mm2_platform STATIC
    Platform.cpp
    Clock.cpp
    Window.cpp
    Input.cpp
    ForceFeedback.cpp
    Dialogs.cpp
    ImGuiPlatform.cpp)
target_link_libraries(mm2_platform
    PUBLIC OpenMM2::core OpenMM2::imgui SDL3::SDL3)
add_library(OpenMM2::platform ALIAS mm2_platform)
