# Race rules and HUD (src/game/session); compiled into mm2_game.
file(GLOB game_session_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
target_sources(mm2_game PRIVATE ${game_session_sources})
target_link_libraries(mm2_game PUBLIC OpenMM2::ui)
