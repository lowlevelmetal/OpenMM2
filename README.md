# OpenMM2

OpenMM2 is an open source reimplementation of **Midtown Madness 2** (Angel
Studios / Microsoft, 2000) for modern Linux and Windows systems.

The main goal is preservation: the original game's mechanics (vehicle
handling, AI, game modes and rules) should behave as they did in 2000. On top
of that, OpenMM2 adds:

- any resolution and aspect ratio, windowed or fullscreen, HiDPI aware
- a Vulkan renderer with an automatic OpenGL fallback
- online multiplayer over UDP with automatic port forwarding (UPnP)
- a Windows installer, and native Linux builds

> **Status:** early but playable. London and San Francisco, cruise, Blitz,
> checkpoint and circuit races, the crash courses, Cops and Robbers and online
> multiplayer all run from the original game data. Behaviour is checked
> against the original game function by function; the results, and the gaps
> still open (mostly in network play), are in [docs/parity/](docs/parity/README.md).

## You need the original game

OpenMM2 contains **no** Midtown Madness 2 content: no models, textures,
sounds, music or text. It loads them from your own copy of the game, which can
be any of:

- the original CD in a drive (or a copy of its contents)
- a disc image: `.iso`, `.bin`/`.cue`, `.img` (2048- and 2352-byte sectors)
- an existing Midtown Madness 2 installation folder (the one containing
  `MM2CORE.AR`)

OpenMM2 never runs, modifies or bypasses the copy protection of the original
executable, and it does not need a CD key. Optionally the game archives
(about 400 MB) are copied to the hard disk once, so the disc or image is not
needed afterwards.

This project is not affiliated with or endorsed by Microsoft, Rockstar Games
or Angel Studios. "Midtown Madness" is a trademark of its respective owner.

## Installing

### Windows

Run `OpenMM2-<version>-windows-x86_64-setup.exe` (64-bit Windows 7 or later).
The installer asks where your Midtown Madness 2 files are and checks them
before continuing. You can also skip that step; OpenMM2 asks on first start.

Silent installs accept:

| Option | Meaning |
| --- | --- |
| `/S` | silent install |
| `/D=C:\Games\OpenMM2` | install directory (must be last, unquoted) |
| `/SOURCE=<path>` | game data: disc image, drive root such as `D:\`, or install folder |
| `/COPYDATA` | copy the game archives to `<install dir>\gamedata` |

The exit code is 0 on success and 2 if the program was installed but the
`/SOURCE` was rejected.

The portable ZIP (`OpenMM2-<version>-windows-x86_64-portable.zip`) needs no
installation: unpack it anywhere. Because it contains `portable.txt`, settings
are kept in a `user` folder next to `openmm2.exe`.

### Linux

Unpack `OpenMM2-<version>-linux-x86_64.tar.gz` and run `bin/openmm2`, or
build from source (below). `cmake --install` puts a desktop entry, AppStream
metadata and icons in the usual places.

### Pointing OpenMM2 at the game

On first start OpenMM2 shows a setup screen for choosing the game data (run
`openmm2 --setup` to show it again). It can also be done from a terminal:

```sh
openmm2 --check-source ~/Downloads/midtown2.iso           # is this usable?
openmm2 --import-source /run/media/$USER/MIDTOWN2          # copy archives to the user data folder
openmm2 --source /path/to/midtown2.iso                     # use for this run only
```

Settings live in `openmm2.ini`:

| Platform | Settings | Data (imported game files, saves, logs) |
| --- | --- | --- |
| Linux | `$XDG_CONFIG_HOME/openmm2` (`~/.config/openmm2`) | `$XDG_DATA_HOME/openmm2` (`~/.local/share/openmm2`) |
| Windows | `%APPDATA%\OpenMM2` | `%LOCALAPPDATA%\OpenMM2` |
| Portable | `user\` next to the executable | `user\` next to the executable |

The Windows installer records its choice in `openmm2-install.ini` next to
`openmm2.exe`; per-user settings take precedence over it.

## Building from source

Requirements:

- CMake 3.25 or newer and Ninja
- a C++23 compiler: GCC 14+, Clang 18+ (with libstdc++ 14+ or libc++ 18+),
  or Visual Studio 2022 17.10+
- `glslc` (shaderc) or `glslangValidator` (glslang) for the Vulkan shaders
- Git (dependencies are fetched at configure time)

Other dependencies (SDL3, miniz, ENet, miniupnpc, Dear ImGui, Vulkan headers,
volk, VMA, GoogleTest) are downloaded and built automatically at pinned
versions. On Linux, installed SDL3 and GoogleTest packages are used when
available (`-DOPENMM2_USE_SYSTEM_DEPS=OFF` to always build them).

### Linux

```sh
# Arch Linux
sudo pacman -S --needed base-devel cmake ninja git shaderc glslang sdl3 gtest
# Fedora
sudo dnf install cmake ninja-build gcc-c++ git glslc glslang SDL3-devel gtest-devel
# Debian/Ubuntu (25.04 or newer for a recent enough compiler and SDL3)
sudo apt install build-essential cmake ninja-build git glslc glslang-tools libsdl3-dev libgtest-dev

cmake --preset linux-gcc-release          # or linux-clang-release, *-debug
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release
```

Binaries end up in `out/build/<preset>/bin`. Without presets, the usual
`cmake -S . -B build -G Ninja && cmake --build build` works as well.

### Windows (MSVC)

Install Visual Studio 2022 with the "Desktop development with C++" workload
and the [Vulkan SDK](https://vulkan.lunarg.com/) (for `glslc`). From a
"x64 Native Tools Command Prompt":

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

Visual Studio can also open the folder directly and use the same presets;
`windows-vs2022` generates a `.sln` instead.

### Cross-compiling for Windows (MinGW-w64)

```sh
sudo pacman -S mingw-w64-gcc        # Debian/Ubuntu: mingw-w64 (GCC 14+ needed)
cmake --preset mingw-cross-release
cmake --build --preset mingw-cross-release
```

If Wine is installed, `ctest --preset mingw-cross-release` runs the tests
through it.

### Tests with the original game data

Tests that need the retail files are skipped unless `OPENMM2_GAME_DATA` points
at a disc image, disc or installation:

```sh
OPENMM2_GAME_DATA=~/Downloads/midtown2.iso ctest --preset linux-gcc-release
```

### Packages

```sh
cpack --preset linux-release                              # .tar.gz (Linux)
cpack --preset windows-portable                           # portable .zip (MSVC)
cmake --build --preset windows-msvc-release --target package_installer   # setup .exe
```

The installer is built from `packaging/windows/openmm2.nsi` and needs NSIS 3
(`makensis`); CMake finds it in `Program Files\NSIS` or on `PATH`. A native
`makensis` also works when cross-compiling. The icon artwork is in
`packaging/icons` (`render-icons.sh` regenerates the bitmaps from the SVG).

## mm2tool

`mm2tool` is a developer tool for looking inside the game data:

```sh
mm2tool source                                   # list detected game sources
mm2tool ls midtown2.iso 'tune/*.vehcarsim'       # all archives mounted as the game sees them
mm2tool cat midtown2.iso tune/vehicle/vpbug.vehcarsim
mm2tool extract midtown2.iso local/extracted 'city/*'
```

Run `mm2tool` without arguments for the full list of commands. Keep anything
you extract out of the repository (the `local/` folder is ignored by Git).

## Contributing

See [CLAUDE.md](CLAUDE.md) for the project rules and code layout, and
[docs/](docs/) for architecture notes and file format documentation. In short:
no game data in the repository, no circumvention of the original game's copy
protection, and behaviour that is inferred rather than known is marked as such.

## License

OpenMM2 is free software, released under the GNU General Public License,
version 3 or later (see [LICENSE](LICENSE)). Binary packages include the
licences of the bundled third-party libraries in `licenses/`.
