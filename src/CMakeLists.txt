# Modules, in dependency order. A module is built when its directory has a
# CMakeLists.txt, so subsystems can land independently.
#
#   core      logging, config, files, math (no third-party deps)
#   vfs       disc images, directories, DAVE archives, mount stack
#   data      generic Angel text formats (tune "type: a" files, CSV, key=value)
#   asset     models (PKG), textures (TEX), bounds (BND/BBND/TER), matrices (MTX)
#   city      PSDL, INST, CPVS, BAI, path sets, race/AI map files, lighting
#   platform  SDL3 window, display modes, input, timing
#   render    RHI with Vulkan and OpenGL backends
#   audio     mixer, WAV streaming, 3D sound
#   video     AVI demuxing, Indeo 5 decoding (the intro movie)
#   phys      rigid bodies, collision, vehicle simulation
#   ai        road network, traffic, pedestrians, opponents, police
#   net       ENet transport, UPnP port mapping, LAN discovery, sessions
#   ui        fonts, text layout, menu widgets in the original's 640x480 space
#   game      game modes, AI, HUD, menus
#   app       the OpenMM2 executable and first-run setup
#
# OPENMM2_MODULES is defined in the top-level CMakeLists.txt.
foreach(module IN LISTS OPENMM2_MODULES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${module}/CMakeLists.txt")
        add_subdirectory(${module})
    endif()
endforeach()
