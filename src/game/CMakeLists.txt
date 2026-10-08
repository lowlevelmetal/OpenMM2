# Sources: every *.cpp in this directory.
file(GLOB game_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(mm2_game STATIC ${game_sources})
target_link_libraries(mm2_game
    PUBLIC OpenMM2::core OpenMM2::vfs OpenMM2::data OpenMM2::city OpenMM2::asset OpenMM2::render OpenMM2::phys
           OpenMM2::ai)
add_library(OpenMM2::game ALIAS mm2_game)

# Multiplayer adapter (src/game/net): net::Session + port mapping + LAN browser.
file(GLOB game_net_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp")
target_sources(mm2_game PRIVATE ${game_net_sources})
target_link_libraries(mm2_game PUBLIC OpenMM2::net)
add_subdirectory(session)

# Effects and breakable props (src/game/fx, src/game/bangers).
file(GLOB game_fx_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fx/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/bangers/*.cpp")
target_sources(mm2_game PRIVATE ${game_fx_sources})

# The city's moving and scripted objects: gizmos, parked cars, cable cars
# (src/game/world). They drive the ambient sound objects of src/audio/game.
file(GLOB game_world_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/world/*.cpp")
target_sources(mm2_game PRIVATE ${game_world_sources})
target_link_libraries(mm2_game PUBLIC OpenMM2::audio)
