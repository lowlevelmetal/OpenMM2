; Files that the installers of OpenMM2 0.4.1 and earlier put in the install
; directory. Those versions wrote no install-manifest.txt, so this list is how
; an update from them (and an uninstaller that finds no manifest) knows what
; to remove. It is the union of the release installers' contents (0.1.0 to
; 0.4.0, all MSVC builds with the same file set) and of the packaging rules in
; git history; 0.4.1 ships the same rules. Never add gamedata\ or
; openmm2-install.ini: they belong to the user's game data setup.
;
;   !insertmacro OPENMM2_LEGACY_UNINSTALL <dir>

!ifndef OPENMM2_LEGACY_FILES_NSH
!define OPENMM2_LEGACY_FILES_NSH

!macro OPENMM2_LEGACY_UNINSTALL base
    Delete "${base}\openmm2.exe"
    Delete "${base}\mm2tool.exe"
    Delete "${base}\uninstall.exe"
    Delete "${base}\LICENSE.txt"
    Delete "${base}\README.md"
    ; MSVC runtime (InstallRequiredSystemLibraries, Visual Studio 2022).
    Delete "${base}\concrt140.dll"
    Delete "${base}\msvcp140.dll"
    Delete "${base}\msvcp140_1.dll"
    Delete "${base}\msvcp140_2.dll"
    Delete "${base}\msvcp140_atomic_wait.dll"
    Delete "${base}\msvcp140_codecvt_ids.dll"
    Delete "${base}\vcruntime140.dll"
    Delete "${base}\vcruntime140_1.dll"
    ; Fallback fonts (cmake/Fonts.cmake).
    Delete "${base}\fonts\GilliusADF-COPYING.txt"
    Delete "${base}\fonts\GilliusADFNo2-Bold.otf"
    Delete "${base}\fonts\GilliusADFNo2-Regular.otf"
    Delete "${base}\fonts\LiberationSans-Bold.ttf"
    Delete "${base}\fonts\LiberationSans-LICENSE.txt"
    Delete "${base}\fonts\LiberationSans-Regular.ttf"
    ; Licences of the bundled dependencies (cmake/Packaging.cmake).
    Delete "${base}\licenses\dmusic.txt"
    Delete "${base}\licenses\enet.txt"
    Delete "${base}\licenses\imgui.txt"
    Delete "${base}\licenses\miniupnpc.txt"
    Delete "${base}\licenses\miniz.txt"
    Delete "${base}\licenses\sdl3.txt"
    Delete "${base}\licenses\stb.txt"
    Delete "${base}\licenses\volk.txt"
    Delete "${base}\licenses\vulkanheaders.txt"
    Delete "${base}\licenses\vulkanmemoryallocator.txt"
    RMDir "${base}\fonts"
    RMDir "${base}\licenses"
!macroend

!endif ; OPENMM2_LEGACY_FILES_NSH
