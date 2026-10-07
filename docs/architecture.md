# Architecture

OpenMM2 is split into static-library modules under `src/`, linked into the
`openmm2` executable and the `mm2tool` developer tool. Lower modules never
depend on higher ones.

```
            app ──────────────────────────────┐
             │                                │
            game ─────┬────────┬──────┬───────┤
             │        │        │      │       │
           render   audio     net   phys      │
             │        │              │        │
          platform    │            data       │
                      │              │        │
   asset ── city ─────┴───── data ───┴── vfs ─┴── core
```

| Module | Responsibility |
|--------|----------------|
| core | logging, INI, files, paths, strings, Angel-convention math |
| vfs | disc images (ISO 9660/Joliet, raw BIN), folders, DAVE archives, mount stack, game-source detection |
| data | Angel `type: a` tune files, CSV, key=value files, PE string tables |
| asset | textures (TEX/TGA/JPG), models (PKG), bounds (BND/BBND/TER), MTX |
| city | PSDL streets, instances, PVS, AI road network, races, lighting |
| platform | SDL3 window, displays, input devices, timing, dialogs |
| render | RHI with Vulkan and OpenGL backends, 2D overlay, ImGui |
| audio | WAV decoding, software mixer with DirectSound3D semantics, SDL3 output |
| phys | rigid bodies, collision, vehicle simulation |
| net | ENet transport, UPnP/NAT-PMP port mapping, LAN discovery, lobby, replication |
| game | catalogs, game modes, AI, HUD, menus, player records |
| app | command line, settings, first-run setup, main loop |

## Game data

The original game is never modified or executed. At startup the configured
*game source* (`[GameData] Source` in the user's `openmm2.ini`, or the
installer's `openmm2-install.ini`) is probed (`vfs::probeGameSource`) and its
archives are mounted into one `vfs::Vfs`. Every later file access goes
through that mount stack using the game's own virtual paths
(`tune/vehicle/vpbug.vehcarsim`). The UI strings come from `MMLANG.DLL`,
which is parsed as data.

## Timing

The simulation runs on a fixed time step; rendering interpolates between the
last two simulation states so any refresh rate works. Multiplayer exchanges
simulation state at a fixed rate independent of the frame rate.

## Accuracy

Behaviour is reproduced from evidence: the game's own data files, Open1560
(the Midtown Madness 1 reimplementation on the same engine), community
documentation and observation of the original game. Anything inferred is
marked as such in code comments and in `docs/`, so it can be checked later.
