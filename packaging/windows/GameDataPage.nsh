; "Original game data" installer page.
;
; Lets the user pick where OpenMM2 finds the original Midtown Madness 2 files
; (disc image, disc drive, existing installation, or skip) and whether to copy
; the archives to the hard disk. The choice is validated by running the
; bundled `openmm2.exe --check-source <path>` from $PLUGINSDIR, so the page can
; come before the files are installed.
;
; Inputs/outputs (declared in openmm2.nsi):
;   $GameSource  chosen path, "" to skip
;   $CopyData    1 to import the archives into $INSTDIR\gamedata
; Input (declared here):
;   $GD_NextIsInstall  1 when installation starts right after this page (an
;                      update skips the directory page), so Next reads Install
; Requires the section index ${SecGameData} and the define STAGE_DIR.

!include nsDialogs.nsh
!include FileFunc.nsh
!include TextFunc.nsh
!include LogicLib.nsh

; Rough size of the four retail archives, in KiB, for the disk space check.
!define GAMEDATA_SIZE_KB 390000

Var GD_Dialog
Var GD_RadioImage
Var GD_ImagePath
Var GD_ImageBrowse
Var GD_RadioDisc
Var GD_DiscList
Var GD_RadioFolder
Var GD_FolderPath
Var GD_FolderBrowse
Var GD_RadioSkip
Var GD_Copy
; State kept across visits of the page.
Var GD_Mode       ; image | disc | folder | skip
Var GD_Image
Var GD_Disc       ; drive root, e.g. "D:\"
Var GD_Folder
Var GD_CopyState  ; 0/1
Var GD_Initialized
Var GD_ExeReady
Var GD_NextIsInstall

; Fills the state from $GameSource (previous install or /SOURCE=) or from
; whatever can be found on this machine.
Function GD_InitState
    ${If} $GD_Initialized == 1
        Return
    ${EndIf}
    StrCpy $GD_Initialized 1
    StrCpy $GD_CopyState 0
    StrCpy $GD_Mode "image"

    ; Drives that hold the game disc right now.
    StrCpy $GD_Disc ""
    ${GetDrives} "CDROM" "GD_FindDiscCallback"

    ${If} $GameSource != ""
        StrLen $0 $GameSource
        StrCpy $1 $GameSource "" 1
        ${If} $0 == 3
        ${AndIf} $1 == ":\"
            StrCpy $GD_Mode "disc"
            StrCpy $GD_Disc $GameSource
        ${ElseIf} ${FileExists} "$GameSource\*.*"
            StrCpy $GD_Mode "folder"
            StrCpy $GD_Folder $GameSource
        ${Else}
            StrCpy $GD_Mode "image"
            StrCpy $GD_Image $GameSource
        ${EndIf}
    ${ElseIf} $GD_Disc != ""
        StrCpy $GD_Mode "disc"
        StrCpy $GD_CopyState 1
    ${Else}
        ; A disc image in the Downloads folder is the most common case today.
        ClearErrors
        FindFirst $0 $1 "$PROFILE\Downloads\*idtown*.iso"
        ${IfNot} ${Errors}
            StrCpy $GD_Image "$PROFILE\Downloads\$1"
        ${EndIf}
        FindClose $0
    ${EndIf}
FunctionEnd

Function GD_FindDiscCallback
    ${If} $GD_Disc == ""
    ${AndIf} ${FileExists} "$9GAME\MM2CORE.AR"
        StrCpy $GD_Disc $9
    ${EndIf}
    Push $0 ; continue enumerating
FunctionEnd

Function GD_AddDriveCallback
    ; $9 = "D:\"; show the volume label when there is a disc.
    System::Call 'kernel32::GetVolumeInformationW(w r9, w .r7, i ${NSIS_MAX_STRLEN}, p 0, p 0, p 0, p 0, i 0) i .r6'
    ${If} $6 != 0
    ${AndIf} $7 != ""
        ${NSD_CB_AddString} $GD_DiscList "$9  ($7)"
    ${Else}
        ${NSD_CB_AddString} $GD_DiscList "$9"
    ${EndIf}
    Push $0
FunctionEnd

Function GameDataPageCreate
    ; Silent installs and re-validation in the section use $INSTDIR; the page
    ; runs before installation, so extract a private copy of the program.
    ${If} $GD_ExeReady != 1
        InitPluginsDir
        SetOutPath "$PLUGINSDIR\app"
        File "${STAGE_DIR}\openmm2.exe"
        ; Runtime DLLs, if the build has any (MSVC builds; static MinGW has none).
        !pragma warning push
        !pragma warning disable 7010 ; "no files found"
        File /nonfatal "${STAGE_DIR}\*.dll"
        !pragma warning pop
        StrCpy $GD_ExeReady 1
    ${EndIf}
    Call GD_InitState

    !insertmacro MUI_HEADER_TEXT "Original game data" "Choose where OpenMM2 finds your copy of Midtown Madness 2."

    nsDialogs::Create 1018
    Pop $GD_Dialog
    ${If} $GD_Dialog == error
        Abort
    ${EndIf}
    ${If} $GD_NextIsInstall == 1
        GetDlgItem $0 $HWNDPARENT 1
        SendMessage $0 ${WM_SETTEXT} 0 "STR:$(^InstallBtn)"
    ${EndIf}

    ${NSD_CreateLabel} 0 0 100% 18u "OpenMM2 contains no game content. It uses the files from your original Midtown Madness 2 disc. Where are they?"
    Pop $0

    ${NSD_CreateFirstRadioButton} 0 20u 100% 10u "Disc &image file (.iso, .cue, .bin, .img)"
    Pop $GD_RadioImage
    ${NSD_OnClick} $GD_RadioImage GD_OnModeClick
    ${NSD_CreateText} 12u 31u 226u 12u "$GD_Image"
    Pop $GD_ImagePath
    ${NSD_CreateBrowseButton} 242u 30u 58u 14u "&Browse..."
    Pop $GD_ImageBrowse
    ${NSD_OnClick} $GD_ImageBrowse GD_OnBrowseImage

    ${NSD_CreateAdditionalRadioButton} 0 47u 100% 10u "Game &disc in drive"
    Pop $GD_RadioDisc
    ${NSD_OnClick} $GD_RadioDisc GD_OnModeClick
    ${NSD_CreateDropList} 12u 58u 140u 60u ""
    Pop $GD_DiscList
    ${GetDrives} "CDROM" "GD_AddDriveCallback"
    SendMessage $GD_DiscList ${CB_GETCOUNT} 0 0 $0
    ${If} $0 == 0
        ${NSD_CB_AddString} $GD_DiscList "(no CD/DVD drive found)"
        SendMessage $GD_DiscList ${CB_SETCURSEL} 0 0
        EnableWindow $GD_RadioDisc 0
        ${If} $GD_Mode == "disc"
            StrCpy $GD_Mode "image"
        ${EndIf}
    ${ElseIf} $GD_Disc != ""
        ${NSD_CB_SelectString} $GD_DiscList "$GD_Disc"
    ${Else}
        SendMessage $GD_DiscList ${CB_SETCURSEL} 0 0
    ${EndIf}

    ${NSD_CreateAdditionalRadioButton} 0 74u 100% 10u "Existing Midtown Madness 2 &installation folder"
    Pop $GD_RadioFolder
    ${NSD_OnClick} $GD_RadioFolder GD_OnModeClick
    ${NSD_CreateText} 12u 85u 226u 12u "$GD_Folder"
    Pop $GD_FolderPath
    ${NSD_CreateBrowseButton} 242u 84u 58u 14u "B&rowse..."
    Pop $GD_FolderBrowse
    ${NSD_OnClick} $GD_FolderBrowse GD_OnBrowseFolder

    ${NSD_CreateAdditionalRadioButton} 0 101u 100% 10u "&Skip for now (OpenMM2 asks for the game files when it starts)"
    Pop $GD_RadioSkip
    ${NSD_OnClick} $GD_RadioSkip GD_OnModeClick

    ${NSD_CreateCheckBox} 0 115u 100% 20u "&Copy the game data to this computer (about 400 MB; the disc or image is not needed afterwards)"
    Pop $GD_Copy
    ${If} $GD_CopyState == 1
        ${NSD_Check} $GD_Copy
    ${EndIf}

    Call GD_ApplyMode
    GetFunctionAddress $0 GD_OnBack
    nsDialogs::OnBack $0
    nsDialogs::Show
FunctionEnd

; Checks the radio button for $GD_Mode and enables the matching inputs.
Function GD_ApplyMode
    ${NSD_Uncheck} $GD_RadioImage
    ${NSD_Uncheck} $GD_RadioDisc
    ${NSD_Uncheck} $GD_RadioFolder
    ${NSD_Uncheck} $GD_RadioSkip
    StrCpy $0 0
    StrCpy $1 0
    StrCpy $2 0
    StrCpy $3 1
    ${Select} $GD_Mode
    ${Case} "image"
        ${NSD_Check} $GD_RadioImage
        StrCpy $0 1
    ${Case} "disc"
        ${NSD_Check} $GD_RadioDisc
        StrCpy $1 1
    ${Case} "folder"
        ${NSD_Check} $GD_RadioFolder
        StrCpy $2 1
    ${CaseElse}
        ${NSD_Check} $GD_RadioSkip
        StrCpy $3 0
    ${EndSelect}
    EnableWindow $GD_ImagePath $0
    EnableWindow $GD_ImageBrowse $0
    EnableWindow $GD_DiscList $1
    EnableWindow $GD_FolderPath $2
    EnableWindow $GD_FolderBrowse $2
    EnableWindow $GD_Copy $3
FunctionEnd

Function GD_OnModeClick
    Pop $0 ; clicked control
    ${If} $0 == $GD_RadioImage
        StrCpy $GD_Mode "image"
    ${ElseIf} $0 == $GD_RadioDisc
        StrCpy $GD_Mode "disc"
    ${ElseIf} $0 == $GD_RadioFolder
        StrCpy $GD_Mode "folder"
    ${Else}
        StrCpy $GD_Mode "skip"
    ${EndIf}
    Call GD_ApplyMode
FunctionEnd

Function GD_OnBrowseImage
    Pop $0
    ${NSD_GetText} $GD_ImagePath $0
    nsDialogs::SelectFileDialog open "$0" "Disc images (*.iso;*.cue;*.bin;*.img)|*.iso;*.cue;*.bin;*.img|All files (*.*)|*.*"
    Pop $0
    ${If} $0 != ""
    ${AndIf} $0 != "error"
        ${NSD_SetText} $GD_ImagePath $0
        StrCpy $GD_Mode "image"
        Call GD_ApplyMode
    ${EndIf}
FunctionEnd

Function GD_OnBrowseFolder
    Pop $0
    ${NSD_GetText} $GD_FolderPath $0
    nsDialogs::SelectFolderDialog "Select the folder that contains MM2CORE.AR" "$0"
    Pop $0
    ${If} $0 != ""
    ${AndIf} $0 != "error"
        ${NSD_SetText} $GD_FolderPath $0
        StrCpy $GD_Mode "folder"
        Call GD_ApplyMode
    ${EndIf}
FunctionEnd

; Saves the page's inputs into the GD_* state variables.
Function GD_SaveState
    ${NSD_GetText} $GD_ImagePath $GD_Image
    ${NSD_GetText} $GD_FolderPath $GD_Folder
    ${NSD_GetText} $GD_DiscList $0
    StrCpy $1 $0 1
    ${If} $1 == "("
        StrCpy $GD_Disc "" ; "(no CD/DVD drive found)"
    ${Else}
        StrCpy $GD_Disc $0 3 ; "D:\  (MIDTOWN2)" -> "D:\"
    ${EndIf}
    ${NSD_GetState} $GD_Copy $0
    ${If} $0 == ${BST_CHECKED}
        StrCpy $GD_CopyState 1
    ${Else}
        StrCpy $GD_CopyState 0
    ${EndIf}
FunctionEnd

Function GD_OnBack
    Call GD_SaveState
FunctionEnd

Function GameDataPageLeave
    Call GD_SaveState

    ${If} $GD_Mode == "skip"
        StrCpy $GameSource ""
        StrCpy $CopyData 0
        SectionSetSize ${SecGameData} 0
        Return
    ${EndIf}

    ${Select} $GD_Mode
    ${Case} "image"
        StrCpy $0 $GD_Image
    ${Case} "disc"
        StrCpy $0 $GD_Disc
    ${CaseElse}
        StrCpy $0 $GD_Folder
    ${EndSelect}
    ${If} $0 == ""
        MessageBox MB_ICONEXCLAMATION|MB_OK "Choose where the Midtown Madness 2 files are, or select Skip."
        Abort
    ${EndIf}

    Push $0
    Call QuoteArg
    Pop $1
    nsExec::ExecToStack '"$PLUGINSDIR\app\openmm2.exe" --check-source $1'
    Pop $2 ; exit code, or "error"/"timeout"
    Pop $3 ; output
    ${TrimNewLines} "$3" $3
    ${If} $2 != 0
        ${If} $2 == "error"
            StrCpy $3 "The check could not be run."
        ${ElseIf} $3 == ""
            StrCpy $3 "No usable Midtown Madness 2 files were found there."
        ${EndIf}
        MessageBox MB_ICONEXCLAMATION|MB_OK "OpenMM2 cannot use this game data:$\n$\n$3$\n$\nChoose another location, or select Skip to set it up later."
        Abort
    ${EndIf}

    StrCpy $GameSource $0
    ${If} $GD_CopyState == 1
        StrCpy $CopyData 1
        SectionSetSize ${SecGameData} ${GAMEDATA_SIZE_KB}
    ${Else}
        StrCpy $CopyData 0
        SectionSetSize ${SecGameData} 0
    ${EndIf}
FunctionEnd
