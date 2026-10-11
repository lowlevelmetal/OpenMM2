; OpenMM2 Windows installer (NSIS 3, Unicode, Modern UI 2).
;
; Normally built by the `package_installer` CMake target (cmake/Packaging.cmake),
; which stages the install tree and runs makensis with:
;   -DVERSION=<x.y.z>            product version
;   -DSTAGE_DIR=<dir>            staged "app" component (openmm2.exe, DLLs, licences)
;   -DAPP_FILES_NSH=<file>       generated File/Delete lists for STAGE_DIR
;   -DTOOLS_FILES_NSH=<file>     (optional) same for the "tools" component
;   -DOUTFILE=<file>             installer to write
;   -DSOURCE_DIR=<dir>           repository root (licence, icons)
;
; Command line options, in addition to NSIS's /S (silent) and /D=<dir>:
;   /SOURCE=<path>   game data for silent installs: a disc image, a drive
;                    root such as D:\ or a Midtown Madness 2 installation folder
;   /COPYDATA        copy the game archives to <install dir>\gamedata
; Exit codes: 0 success, 1 cancelled or failed, 2 installed but the game data
; given with /SOURCE was rejected (OpenMM2 will ask for it on first start).
;
; Contract with openmm2.exe:
;   openmm2.exe --check-source <path>          exit 0 when usable; prints a
;                                              one-line reason otherwise
;   openmm2.exe --import-source <path> <dir>   copies the archives, printing
;                                              "progress <0-100>" lines; on
;                                              failure it removes its partial
;                                              (.part) files itself
; and the installer writes $INSTDIR\openmm2-install.ini (UTF-8):
;   [GameData]
;   Source=<path>
; which OpenMM2 uses when the per-user configuration names no game data.

Unicode true
ManifestDPIAware true
ManifestSupportedOS all
SetCompressor /SOLID lzma
RequestExecutionLevel admin

!ifndef VERSION
    !error "Pass -DVERSION=<x.y.z>"
!endif
!ifndef STAGE_DIR
    !error "Pass -DSTAGE_DIR=<staged app component>"
!endif
!ifndef APP_FILES_NSH
    !error "Pass -DAPP_FILES_NSH=<generated file list>"
!endif
!ifndef SOURCE_DIR
    !error "Pass -DSOURCE_DIR=<repository root>"
!endif
!ifndef OUTFILE
    !define OUTFILE "OpenMM2-${VERSION}-windows-x64-setup.exe"
!endif

!define PRODUCT "OpenMM2"
!define PUBLISHER "OpenMM2 contributors"
!define HOMEPAGE "https://github.com/lowlevelmetal/OpenMM2"
!define REG_KEY "Software\OpenMM2"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenMM2"
!define ICONS "${SOURCE_DIR}\packaging\icons"

!include MUI2.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include TextFunc.nsh
!include x64.nsh
!include WinVer.nsh
!include "${APP_FILES_NSH}"
!ifdef TOOLS_FILES_NSH
    !include "${TOOLS_FILES_NSH}"
!endif
!include "${__FILEDIR__}\Helpers.nsh"

Name "${PRODUCT}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${PRODUCT}"
BrandingText "${PRODUCT} ${VERSION}"
ShowInstDetails show
ShowUninstDetails show

; Version resource: VIProductVersion needs four numeric parts.
!searchparse /noerrors "${VERSION}" "" _V_MAJOR "." _V_MINOR "." _V_PATCH
!ifndef _V_PATCH
    !define _V_PATCH 0
!endif
VIProductVersion "${_V_MAJOR}.${_V_MINOR}.${_V_PATCH}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${PRODUCT} Setup"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "GNU General Public License v3 or later"

Var GameSource ; chosen game data location, "" to configure later
Var CopyData   ; 1 to copy the archives into $INSTDIR\gamedata
Var DefaultInstDir

; --- Modern UI -------------------------------------------------------------
!define MUI_ICON "${ICONS}\openmm2.ico"
!define MUI_UNICON "${ICONS}\openmm2.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${ICONS}\installer-sidebar.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${ICONS}\installer-sidebar.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${ICONS}\installer-header.bmp"
!define MUI_HEADERIMAGE_UNBITMAP "${ICONS}\installer-header.bmp"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC

!define MUI_WELCOMEPAGE_TEXT "This will install ${PRODUCT} ${VERSION}, an open source reimplementation of Midtown Madness 2.$\r$\n$\r$\n${PRODUCT} does not include any of the original game's content. You need your own copy of Midtown Madness 2 (the CD, a disc image, or an existing installation); setup asks where it is.$\r$\n$\r$\n$_CLICK"
!insertmacro MUI_PAGE_WELCOME

!define MUI_LICENSEPAGE_TEXT_TOP "${PRODUCT} is free software, licensed under the GNU General Public License."
!define MUI_LICENSEPAGE_TEXT_BOTTOM "You do not need to accept this licence to use ${PRODUCT}; it describes your rights to copy, modify and share it. Click Next to continue."
!define MUI_LICENSEPAGE_BUTTON "$(^NextBtn)"
!insertmacro MUI_PAGE_LICENSE "${SOURCE_DIR}\LICENSE"

!insertmacro MUI_PAGE_COMPONENTS
Page custom GameDataPageCreate GameDataPageLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Start ${PRODUCT}"
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchUnelevated
!define MUI_FINISHPAGE_LINK "${PRODUCT} on GitHub"
!define MUI_FINISHPAGE_LINK_LOCATION "${HOMEPAGE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; --- Sections --------------------------------------------------------------

Section "${PRODUCT} (required)" SecGame
    SectionIn RO
    SetOutPath "$INSTDIR"
    !insertmacro OPENMM2_APP_INSTALL
    SetOutPath "$INSTDIR"
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Let the game accept multiplayer connections and LAN discovery (UDP; see
    ; docs/multiplayer.md). Scoped to the executable, not to fixed ports, so a
    ; changed port in the settings keeps working. Failure (e.g. a third-party
    ; firewall) is not fatal: Windows will prompt on first host instead.
    nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${PRODUCT}" program="$INSTDIR\openmm2.exe"'
    Pop $0
    nsExec::ExecToLog 'netsh advfirewall firewall add rule name="${PRODUCT}" dir=in action=allow protocol=UDP program="$INSTDIR\openmm2.exe" enable=yes profile=any'
    Pop $0
SectionEnd

Section "Start menu shortcut" SecStartMenu
    CreateShortcut "$SMPROGRAMS\${PRODUCT}.lnk" "$INSTDIR\openmm2.exe" "" "$INSTDIR\openmm2.exe" 0
SectionEnd

Section "Desktop shortcut" SecDesktop
    CreateShortcut "$DESKTOP\${PRODUCT}.lnk" "$INSTDIR\openmm2.exe" "" "$INSTDIR\openmm2.exe" 0
SectionEnd

!ifdef TOOLS_FILES_NSH
Section /o "Developer tools (mm2tool)" SecTools
    !insertmacro OPENMM2_TOOLS_INSTALL
SectionEnd
!endif

Section "-Game data" SecGameData
    Call InstallGameData
SectionEnd

Section "-Register"
    SetOutPath "$INSTDIR"
    WriteRegStr HKLM "${REG_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "${REG_KEY}" "Version" "${VERSION}"

    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${PRODUCT}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\openmm2.exe,0"
    WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegStr HKLM "${UNINST_KEY}" "URLInfoAbout" "${HOMEPAGE}"
    WriteRegStr HKLM "${UNINST_KEY}" "HelpLink" "${HOMEPAGE}/issues"
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecGame} "The ${PRODUCT} program."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Add ${PRODUCT} to the Start menu."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Add a ${PRODUCT} shortcut to the desktop."
!ifdef TOOLS_FILES_NSH
    !insertmacro MUI_DESCRIPTION_TEXT ${SecTools} "mm2tool, a command line tool to inspect and extract the original game files (for modders and developers)."
!endif
!insertmacro MUI_FUNCTION_DESCRIPTION_END

!include "${__FILEDIR__}\GameDataPage.nsh"

; --- Installer functions ---------------------------------------------------

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP|MB_OK "${PRODUCT} requires a 64-bit version of Windows." /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${IfNot} ${AtLeastWin7}
        MessageBox MB_ICONSTOP|MB_OK "${PRODUCT} requires Windows 7 or later." /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ; Never show "insert a disk" error boxes while probing optical drives.
    System::Call 'kernel32::SetErrorMode(i 1)'
    SetRegView 64
    SetShellVarContext all

    ; Upgrade in place unless /D= chose another directory.
    StrCpy $DefaultInstDir "$PROGRAMFILES64\${PRODUCT}"
    ReadRegStr $0 HKLM "${REG_KEY}" "InstallDir"
    ${If} $0 != ""
    ${AndIf} $INSTDIR == $DefaultInstDir
        StrCpy $INSTDIR $0
    ${EndIf}

    ; Previous choice of game data.
    StrCpy $CopyData 0
    Push "$INSTDIR\openmm2-install.ini"
    Push "Source"
    Call ReadUtf8IniValue
    Pop $GameSource

    ${GetParameters} $R0
    ClearErrors
    ${GetOptions} $R0 "/SOURCE=" $R1
    ${IfNot} ${Errors}
        StrCpy $GameSource $R1
    ${EndIf}
    ClearErrors
    ${GetOptions} $R0 "/COPYDATA" $R1
    ${IfNot} ${Errors}
        StrCpy $CopyData 1
        SectionSetSize ${SecGameData} ${GAMEDATA_SIZE_KB}
    ${EndIf}
FunctionEnd

; Validates $GameSource (again: silent installs never saw the page), copies
; the archives when requested and writes openmm2-install.ini.
Function InstallGameData
    ${If} $GameSource == ""
        DetailPrint "No game data location chosen; ${PRODUCT} will ask for it when it starts."
        Return
    ${EndIf}

    Push $GameSource
    Call QuoteArg
    Pop $R1
    DetailPrint "Checking game data at $GameSource"
    nsExec::ExecToStack '"$INSTDIR\openmm2.exe" --check-source $R1'
    Pop $0
    Pop $1
    ${TrimNewLines} "$1" $1
    ${If} $0 != 0
        DetailPrint "Game data not usable: $1"
        MessageBox MB_ICONEXCLAMATION|MB_OK "The game data at $GameSource cannot be used:$\n$1$\n$\n${PRODUCT} will ask for the game files when it starts." /SD IDOK
        SetErrorLevel 2
        Return
    ${EndIf}

    StrCpy $R2 $GameSource
    ${If} $CopyData == 1
        StrCpy $R3 "$INSTDIR\gamedata"
        ; A folder that is already there holds an earlier copy (an update or
        ; repair): a failed copy must never remove it.
        StrCpy $R5 0
        ${If} ${FileExists} "$R3\*.*"
            StrCpy $R5 1
        ${EndIf}
        DetailPrint "Copying game data to $R3"
        Push $R3
        Call QuoteArg
        Pop $R4
        Push "Copying game data"
        Push '"$INSTDIR\openmm2.exe" --import-source $R1 $R4'
        Call RunWithProgress
        Pop $0
        ${If} $0 == 0
            StrCpy $R2 $R3
        ${Else}
            DetailPrint "Copying failed (exit code $0)"
            ${If} $R5 == 0
                ; Everything in the folder came from this copy.
                RMDir /r "$R3"
                MessageBox MB_ICONEXCLAMATION|MB_OK "Copying the game data failed. ${PRODUCT} will read it from $GameSource instead." /SD IDOK
            ${Else}
                ; The import removed its partial files; the earlier copy is
                ; untouched. Keep using it if it is complete.
                Push $R3
                Call QuoteArg
                Pop $R4
                nsExec::ExecToStack '"$INSTDIR\openmm2.exe" --check-source $R4'
                Pop $0
                Pop $1
                ${If} $0 == 0
                    StrCpy $R2 $R3
                    MessageBox MB_ICONEXCLAMATION|MB_OK "Copying the game data failed. ${PRODUCT} keeps using the copy that is already in $R3." /SD IDOK
                ${Else}
                    MessageBox MB_ICONEXCLAMATION|MB_OK "Copying the game data failed. ${PRODUCT} will read it from $GameSource instead." /SD IDOK
                ${EndIf}
            ${EndIf}
        ${EndIf}
    ${EndIf}

    ClearErrors
    Push "$INSTDIR\openmm2-install.ini"
    Push "; Written by the ${PRODUCT} installer. Settings in %APPDATA%\OpenMM2\openmm2.ini take precedence.$\r$\n[GameData]$\r$\nSource=$R2$\r$\n"
    Call WriteUtf8File
    ${If} ${Errors}
        DetailPrint "Could not write $INSTDIR\openmm2-install.ini"
    ${Else}
        DetailPrint "Game data: $R2"
    ${EndIf}
FunctionEnd

; The installer runs elevated; start the game as the logged-in user instead.
Function LaunchUnelevated
    Exec '"$WINDIR\explorer.exe" "$INSTDIR\openmm2.exe"'
FunctionEnd

; --- Uninstaller -----------------------------------------------------------

Function un.onInit
    SetRegView 64
    SetShellVarContext all
FunctionEnd

Section "Uninstall"
    nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${PRODUCT}" program="$INSTDIR\openmm2.exe"'
    Pop $0
    !insertmacro OPENMM2_APP_UNINSTALL
!ifdef TOOLS_FILES_NSH
    !insertmacro OPENMM2_TOOLS_UNINSTALL
!endif
    Delete "$INSTDIR\openmm2-install.ini"
    Delete "$INSTDIR\uninstall.exe"
    Delete "$SMPROGRAMS\${PRODUCT}.lnk"
    Delete "$DESKTOP\${PRODUCT}.lnk"

    ${If} ${FileExists} "$INSTDIR\gamedata\*.*"
        MessageBox MB_YESNO|MB_ICONQUESTION "Also delete the copy of the Midtown Madness 2 game data in $INSTDIR\gamedata?$\n$\nKeep it if you plan to reinstall ${PRODUCT}." /SD IDNO IDNO keep_gamedata
        RMDir /r "$INSTDIR\gamedata"
    keep_gamedata:
    ${EndIf}

    ; Per-user settings and saves of the user running the uninstaller.
    SetShellVarContext current
    ${If} ${FileExists} "$APPDATA\OpenMM2\*.*"
    ${OrIf} ${FileExists} "$LOCALAPPDATA\OpenMM2\*.*"
        MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Delete your ${PRODUCT} settings, driver profiles and replays too?$\n$\n$APPDATA\OpenMM2$\n$LOCALAPPDATA\OpenMM2" /SD IDNO IDNO keep_userdata
        RMDir /r "$APPDATA\OpenMM2"
        RMDir /r "$LOCALAPPDATA\OpenMM2"
    keep_userdata:
    ${EndIf}
    SetShellVarContext all

    RMDir "$INSTDIR"
    DeleteRegKey HKLM "${UNINST_KEY}"
    DeleteRegKey HKLM "${REG_KEY}"
SectionEnd
