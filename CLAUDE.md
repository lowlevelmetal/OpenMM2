# OpenMM2

Open source (GPL-3.0) reimplementation of Midtown Madness 2 (Angel Studios /
Microsoft, 2000). Goals, in priority order: accuracy and preservation of the
original game's behaviour, then portability (Linux + Windows), then modern
conveniences (any resolution, Vulkan with OpenGL fallback, online multiplayer
with UPnP).

## Hard rules

- **No DRM circumvention.** Never decrypt, unwrap, dump or disassemble the
  SafeDisc-protected retail executable (`MIDTOWN2.EXE` / `MIDTOWN2.ICD`), and
  never analyse CD-key formats. Learn about the game only from unencrypted data
  (the `.AR` archives, plaintext PE metadata such as imports and resources),
  public community documentation and observed behaviour.
- **No game data in git.** Original files, extracted assets and anything
  derived from them stay under the gitignored `local/` directory. Tests that
  need retail data read `$OPENMM2_GAME_DATA` and skip when it is unset.
- Reference projects: Open1560 (GPL-3.0, Midtown Madness 1 reimplementation on
  the same Angel engine) may be ported with attribution. Its C++ covers only
  what has been rewritten so far; everything else (including the full MM1 car
  physics and collision) is in `code/midtown/game.asm` as symbol-named MASM.
  Grep it before concluding something is missing. Port it to readable C++ with
  the same operation order and 32-bit float math, then adapt only where MM2
  data proves a difference. mm2hook has no licence: use it as documentation of
  layouts and behaviour only, never copy code.

## Layout

- `src/<module>/` — one static library `mm2_<module>` per module, built when
  the directory has a CMakeLists.txt. Order and purpose are listed in
  `src/CMakeLists.txt`; the list itself is `OPENMM2_MODULES` in the root
  CMakeLists.txt.
- `tests/<module>/test_*.cpp` — gtest executable `test_<module>`.
  `tests/TestData.h` provides `MM2_REQUIRE_GAME_DATA()` / `test::gameData()`.
- `tools/mm2tool/cmd_*.cpp` — self-registering subcommands (see `Command.h`).
- `docs/formats/*.md` — file format notes, written as formats are verified.
- `packaging/` — Windows installer (NSIS) and Linux desktop integration.

## Conventions

- C++23, CMake ≥ 3.25, warnings clean on GCC, Clang and MSVC.
- Namespaces `mm2::<module>`; files `PascalCase.cpp/.h`; members `m_name`.
- Formatting: `.clang-format` (4 spaces, 110 columns).
- Math follows the Angel engine conventions in `src/core/Math.h`: row
  vectors, `Mat34` = 3 basis rows + position, Y up, objects face -Z, 32-bit
  floats.
- Parsers take bytes (`std::span<const std::byte>`) and return
  `std::optional`/`bool` plus an error string; they never throw or abort on
  malformed input.
- When behaviour is inferred rather than known, say so in a comment and in the
  format doc, so it can be revisited.

## Building

```
cmake -S . -B build -G Ninja
cmake --build build
OPENMM2_GAME_DATA=/path/to/midtown2.iso ctest --test-dir build
```

`mm2tool` inspects game data, e.g. `mm2tool ls midtown2.iso 'tune/*'`.
