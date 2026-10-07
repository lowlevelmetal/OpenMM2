; Helper functions for the OpenMM2 installer.
;
; All functions preserve the registers they use, except for documented
; outputs. They rely only on plug-ins shipped with NSIS (System, nsExec).

!ifndef OPENMM2_HELPERS_NSH
!define OPENMM2_HELPERS_NSH

!include LogicLib.nsh
!include WinMessages.nsh

; Windows API constants.
!define CP_UTF8 65001
!define WAIT_TIMEOUT 258
!define HANDLE_FLAG_INHERIT 1
!define STARTF_USESTDHANDLES_SHOWWINDOW 0x101
!define CREATE_NO_WINDOW 0x08000000
; Range of the instfiles progress bar as set up by the NSIS UI.
!define NSIS_PROGRESS_RANGE 30000
!define IDC_PROGRESS 1004

; ---------------------------------------------------------------------------
; QuoteArg: quotes a path for a Windows command line so that the child's
; argument parser (MSVC/MinGW CRT rules) sees it verbatim. A trailing
; backslash is doubled, otherwise `"D:\"` would escape the closing quote.
;   Push <path>
;   Call QuoteArg / un.QuoteArg
;   Pop <quoted>
!macro OPENMM2_QUOTEARG un
Function ${un}QuoteArg
    Exch $0
    Push $1
    StrCpy $1 $0 1 -1
    ${If} $1 == "\"
        StrCpy $0 "$0\"
    ${EndIf}
    StrCpy $0 '"$0"'
    Pop $1
    Exch $0
FunctionEnd
!macroend
!insertmacro OPENMM2_QUOTEARG ""

; ---------------------------------------------------------------------------
; WriteUtf8File: writes a string to a file as UTF-8 without BOM. (WriteINIStr
; would encode non-ASCII paths in the ANSI code page, which OpenMM2's INI
; reader does not use.)
;   Push <path>
;   Push <contents>
;   Call WriteUtf8File
; Sets the error flag on failure.
Function WriteUtf8File
    Exch $1 ; contents
    Exch
    Exch $0 ; path
    Push $2
    Push $3
    Push $4
    Push $5

    System::Call 'kernel32::WideCharToMultiByte(i ${CP_UTF8}, i 0, w r1, i -1, p 0, i 0, p 0, p 0) i .r3'
    System::Alloc $3
    Pop $4
    System::Call 'kernel32::WideCharToMultiByte(i ${CP_UTF8}, i 0, w r1, i -1, p r4, i r3, p 0, p 0) i .r3'
    IntOp $3 $3 - 1 ; drop the terminating NUL

    ClearErrors
    FileOpen $2 "$0" w
    ${IfNot} ${Errors}
        System::Call 'kernel32::WriteFile(p r2, p r4, i r3, *i .r5, p 0) i .r5'
        FileClose $2
        ${If} $5 == 0
            SetErrors
        ${EndIf}
    ${EndIf}
    System::Free $4

    Pop $5
    Pop $4
    Pop $3
    Pop $2
    Pop $0
    Pop $1
FunctionEnd

; ---------------------------------------------------------------------------
; ReadUtf8IniValue: reads `Key=Value` from a UTF-8 INI file (first match in
; any section; keys compare case-insensitively). Returns "" when missing.
;   Push <path>
;   Push <key>
;   Call ReadUtf8IniValue
;   Pop <value>
Function ReadUtf8IniValue
    Exch $1 ; key
    Exch
    Exch $0 ; path
    Push $2
    Push $3
    Push $4
    Push $5
    Push $6
    Push $7
    Push $8

    StrCpy $8 "" ; result
    ClearErrors
    FileOpen $2 "$0" r
    ${IfNot} ${Errors}
        FileSeek $2 0 END $3
        FileSeek $2 0 SET
        ${If} $3 > 0
        ${AndIf} $3 < 65536
            IntOp $4 $3 + 1
            System::Alloc $4
            Pop $4
            System::Call 'kernel32::ReadFile(p r2, p r4, i r3, *i .r5, p 0) i .r6'
            IntOp $6 $4 + $5
            System::Call '*$6(&i1 0)'
            ; Decode into an NSIS string (truncated to NSIS_MAX_STRLEN).
            System::Call 'kernel32::MultiByteToWideChar(i ${CP_UTF8}, i 0, p r4, i -1, w .r3, i ${NSIS_MAX_STRLEN}) i .r6'
            System::Free $4

            ; Scan line by line for "<key>=".
            StrLen $4 $1
            IntOp $4 $4 + 1 ; length of "<key>="
            ${Do}
                ${If} $3 == ""
                    ${ExitDo}
                ${EndIf}
                ; Split off the first line.
                StrCpy $5 0
                StrLen $6 $3
                ${Do}
                    ${If} $5 >= $6
                        ${ExitDo}
                    ${EndIf}
                    StrCpy $7 $3 1 $5
                    ${If} $7 == "$\n"
                        ${ExitDo}
                    ${EndIf}
                    IntOp $5 $5 + 1
                ${Loop}
                StrCpy $7 $3 $5      ; line
                IntOp $5 $5 + 1
                StrCpy $3 $3 "" $5   ; rest
                ; Strip a trailing CR.
                StrCpy $5 $7 1 -1
                ${If} $5 == "$\r"
                    StrCpy $7 $7 -1
                ${EndIf}
                StrCpy $5 $7 $4
                ${If} $5 == "$1="
                    StrCpy $8 $7 "" $4
                    ${ExitDo}
                ${EndIf}
            ${Loop}
        ${EndIf}
        FileClose $2
    ${EndIf}

    StrCpy $0 $8
    Pop $8
    Pop $7
    Pop $6
    Pop $5
    Pop $4
    Pop $3
    Pop $2
    Exch $0 ; restore $0, result on top
    Exch
    Pop $1
FunctionEnd

; ---------------------------------------------------------------------------
; RunWithProgress: runs a command line hidden, streaming its stdout/stderr.
; Lines of the form "progress <0-100>" move the installer's progress bar and
; status text; any other line goes to the details log.
;   Push <status text prefix, e.g. "Copying game data">
;   Push <command line>
;   Call RunWithProgress
;   Pop <exit code, or -1 if the process could not be started>
Var RWP_Prefix
Var RWP_Pending
Var RWP_LastPercent ; last "progress" value seen, -1 if none

Function RunWithProgress
    Exch $9 ; command line
    Exch
    Exch $8 ; status prefix
    Push $0
    Push $1
    Push $2
    Push $3
    Push $4
    Push $5
    Push $6
    Push $7

    StrCpy $RWP_Prefix $8
    StrCpy $RWP_Pending ""
    StrCpy $RWP_LastPercent -1

    ; Anonymous pipe; the child inherits the write end as stdout and stderr.
    System::Call '*(i 12, p 0, i 1) p .r0' ; SECURITY_ATTRIBUTES, bInheritHandle = TRUE
    System::Call 'kernel32::CreatePipe(*p .r1, *p .r2, p r0, i 0) i .r3'
    System::Free $0
    ${If} $3 == 0
        StrCpy $8 -1
        Goto rwp_done
    ${EndIf}
    System::Call 'kernel32::SetHandleInformation(p r1, i ${HANDLE_FLAG_INHERIT}, i 0)'

    ; STARTUPINFOW (68 bytes on x86): STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW, SW_HIDE.
    System::Call '*(i 68, p 0, p 0, p 0, i 0, i 0, i 0, i 0, i 0, i 0, i 0, i ${STARTF_USESTDHANDLES_SHOWWINDOW}, &i2 0, &i2 0, p 0, p 0, p r2, p r2) p .r3'
    System::Call '*(p 0, p 0, i 0, i 0) p .r4' ; PROCESS_INFORMATION
    System::Call 'kernel32::CreateProcessW(p 0, w r9, p 0, p 0, i 1, i ${CREATE_NO_WINDOW}, p 0, p 0, p r3, p r4) i .r5'
    System::Call 'kernel32::CloseHandle(p r2)' ; only the child writes now
    System::Free $3
    ${If} $5 == 0
        System::Free $4
        System::Call 'kernel32::CloseHandle(p r1)'
        StrCpy $8 -1
        Goto rwp_done
    ${EndIf}
    System::Call '*$4(p .r5, p .r6)' ; hProcess, hThread
    System::Free $4
    System::Call 'kernel32::CloseHandle(p r6)'

    System::Alloc 4096
    Pop $7
    ${Do}
        System::Call 'kernel32::WaitForSingleObject(p r5, i 100) i .r0'
        ; Drain whatever is buffered (after the wait, so the final output of an
        ; exited process is always read).
        ${Do}
            System::Call 'kernel32::PeekNamedPipe(p r1, p 0, i 0, p 0, *i .r2, p 0) i .r3'
            ${If} $3 == 0
            ${OrIf} $2 == 0
                ${ExitDo}
            ${EndIf}
            System::Call 'kernel32::ReadFile(p r1, p r7, i 4095, *i .r2, p 0) i .r3'
            ${If} $3 == 0
            ${OrIf} $2 == 0
                ${ExitDo}
            ${EndIf}
            IntOp $3 $7 + $2
            System::Call '*$3(&i1 0)'
            System::Call '*$7(&m4096 .r3)'
            StrCpy $RWP_Pending "$RWP_Pending$3"
            Call RWP_ProcessLines
        ${Loop}
        ${If} $0 != ${WAIT_TIMEOUT}
            ${ExitDo}
        ${EndIf}
    ${Loop}
    System::Free $7

    ${If} $RWP_Pending != ""
        StrCpy $RWP_Pending "$RWP_Pending$\n"
        Call RWP_ProcessLines
    ${EndIf}

    System::Call 'kernel32::GetExitCodeProcess(p r5, *i .r8)'
    System::Call 'kernel32::CloseHandle(p r5)'
    System::Call 'kernel32::CloseHandle(p r1)'

rwp_done:
    Pop $7
    Pop $6
    Pop $5
    Pop $4
    Pop $3
    Pop $2
    Pop $1
    Pop $0
    Exch $8 ; restore $8, exit code on top
    Exch
    Pop $9
FunctionEnd

; Consumes complete lines from $RWP_Pending.
Function RWP_ProcessLines
    Push $0
    Push $1
    Push $2
    Push $3
    Push $4
    ${Do}
        StrLen $1 $RWP_Pending
        StrCpy $0 0
        ${Do}
            ${If} $0 >= $1
                ${ExitDo}
            ${EndIf}
            StrCpy $2 $RWP_Pending 1 $0
            ${If} $2 == "$\n"
                ${ExitDo}
            ${EndIf}
            IntOp $0 $0 + 1
        ${Loop}
        ${If} $0 >= $1
            ${ExitDo} ; no complete line yet
        ${EndIf}
        StrCpy $2 $RWP_Pending $0
        IntOp $0 $0 + 1
        StrCpy $RWP_Pending $RWP_Pending "" $0
        StrCpy $3 $2 1 -1
        ${If} $3 == "$\r"
            StrCpy $2 $2 -1
        ${EndIf}
        ${If} $2 == ""
            ${Continue}
        ${EndIf}

        StrCpy $3 $2 9
        ${If} $3 == "progress "
            StrCpy $3 $2 "" 9
            IntOp $3 $3 + 0
            ${If} $3 < 0
                StrCpy $3 0
            ${ElseIf} $3 > 100
                StrCpy $3 100
            ${EndIf}
            StrCpy $RWP_LastPercent $3
            IntOp $4 $3 * 300 ; NSIS_PROGRESS_RANGE / 100
            FindWindow $0 "#32770" "" $HWNDPARENT
            GetDlgItem $0 $0 ${IDC_PROGRESS}
            SendMessage $0 ${PBM_SETPOS} $4 0
            SetDetailsPrint textonly
            DetailPrint "$RWP_Prefix... $3%"
            SetDetailsPrint both
        ${Else}
            DetailPrint "$2"
        ${EndIf}
    ${Loop}
    Pop $4
    Pop $3
    Pop $2
    Pop $1
    Pop $0
FunctionEnd

!endif ; OPENMM2_HELPERS_NSH
