#!/usr/bin/env bash
# Tests the Windows installer (openmm2.nsi) under Wine: a new installation,
# updates (also from the previous release's installer script), a file removed
# between versions, repair, downgrades, a running game, a failed game data
# copy, a hand-edited manifest, moving with /D= and uninstalling. Each check
# looks at the installed files, install-manifest.txt, openmm2-install.ini,
# the registry and the shortcuts. The wizard itself is driven too: which
# pages a new installation, an update, a repair and a downgrade show, what
# they say, and the questions about a newer version and a running game.
# Wine uses its null graphics driver, so no window ever appears.
#
#   packaging/windows/test-installer.sh <build dir>
#
# <build dir> is a configured and built Windows build (the MinGW cross build,
# or MSVC with INSTALL_CONFIG set); its "app" and "tools" install components
# are staged with cmake --install and packed into installers of made-up
# versions. Game data is synthetic (three small PKZIP archives that pass
# --check-source); no original game files are used.
#
# Needs bash, cmake, git, wine (wineboot, wineserver, winepath), a MinGW-w64
# C compiler for two small helper programs, and makensis. Environment:
#   MAKENSIS=<cmd>      makensis to run (default: makensis)
#   MAKENSIS_WINE=1     MAKENSIS is a Windows makensis.exe to run through Wine
#   MINGW_CC=<cmd>      default: x86_64-w64-mingw32-gcc
#   LEGACY_REV=<rev>    git revision whose installer scripts stand for the last
#                       release without update support (default: v0.4.0;
#                       "none" skips those checks)
#   INSTALL_CONFIG=<c>  --config for cmake --install (multi-config builds)
#   WORK_DIR=<dir>      scratch directory without spaces (default: mktemp -d)
#   WINE=<cmd>          default: wine
#   SCENARIOS="<names>" some of: new updates clean move wizard (default: all)
# Exit status 0 when every check passes. The installers, a log
# (test-installer.log) and the Wine prefixes of failed scenarios stay in the
# work directory.

set -uo pipefail

if [[ $# -ne 1 || ! -f "$1/CMakeCache.txt" ]]; then
    sed -n '2,/^$/s/^# \{0,1\}//p' "$0"
    exit 2
fi

BUILD_DIR=$(cd "$1" && pwd)
SOURCE_DIR=$(cd "$(dirname "$0")/../.." && pwd)
MAKENSIS=${MAKENSIS:-makensis}
MAKENSIS_WINE=${MAKENSIS_WINE:-0}
MINGW_CC=${MINGW_CC:-x86_64-w64-mingw32-gcc}
LEGACY_REV=${LEGACY_REV:-v0.4.0}
LEGACY_VERSION=0.4.0
WINE=${WINE:-wine}
WORK=${WORK_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/openmm2-installer-test.XXXXXX")}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
LOG=$WORK/test-installer.log
TIMEOUT=300
: >"$LOG"

if [[ "$WORK" == *" "* ]]; then
    echo "WORK_DIR must not contain spaces: $WORK" >&2
    exit 2
fi

# Never show windows (see init_prefix); a dialog that a silent run did not
# expect then fails the check by timing out instead of waiting for someone.
unset DISPLAY WAYLAND_DISPLAY
export WINEDEBUG=-all
# No Mono/Gecko installers, and no menu entries on the host for the shortcuts.
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"

CURRENT_VERSION=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD_DIR/CMakeCache.txt")
DUMMY_VERSION=99.0.0 # ships extra files
NEXT_VERSION=99.1.0  # no longer ships them

FAILURES=0
CHECKS=0
pass() {
    CHECKS=$((CHECKS + 1))
    echo "  ok    $*"
}
fail() {
    CHECKS=$((CHECKS + 1))
    FAILURES=$((FAILURES + 1))
    echo "  FAIL  $*"
}
section() {
    echo
    echo "== $*"
    echo "== $*" >>"$LOG"
}
die() {
    echo "error: $*" >&2
    exit 1
}
# check <description> <command...>
check() {
    local what=$1
    shift
    if "$@" >>"$LOG" 2>&1; then pass "$what"; else fail "$what"; fi
}
# check_eq <description> <actual> <expected>
check_eq() {
    if [[ "$2" == "$3" ]]; then pass "$1"; else fail "$1: got '$2', expected '$3'"; fi
}
# check_same_lines <description> <actual file> <expected file>
check_same_lines() {
    if diff -u "$3" "$2" >"$WORK/diff.txt"; then
        pass "$1"
    else
        fail "$1"
        sed 's/^/        /' "$WORK/diff.txt"
    fi
}

# --- Staging and building installers --------------------------------------

section "Building installers (OpenMM2 $CURRENT_VERSION)"
STAGE=$WORK/stage
rm -rf "$STAGE"
install_args=()
[[ -n "${INSTALL_CONFIG:-}" ]] && install_args=(--config "$INSTALL_CONFIG")
cmake --install "$BUILD_DIR" "${install_args[@]}" --component app --prefix "$STAGE/app" --strip >>"$LOG" ||
    die "cmake --install (app) failed"
cmake --install "$BUILD_DIR" "${install_args[@]}" --component tools --prefix "$STAGE/tools" --strip >>"$LOG" ||
    die "cmake --install (tools) failed"
[[ -f "$STAGE/app/openmm2.exe" ]] || die "no openmm2.exe in the app component"
HAVE_TOOLS=0
[[ -f "$STAGE/tools/mm2tool.exe" ]] && HAVE_TOOLS=1

# init_prefix <dir>: a fresh Wine prefix whose windows go nowhere: the null
# graphics driver, not the desktop of whoever runs the tests (Wine would
# otherwise find a Wayland session even without WAYLAND_DISPLAY).
init_prefix() {
    export WINEPREFIX=$1
    rm -rf "$WINEPREFIX"
    "$WINE" wineboot --init >>"$LOG" 2>&1 || die "wineboot failed"
    "$WINE" reg add 'HKCU\Software\Wine\Drivers' /v Graphics /d null /f >>"$LOG" 2>&1 ||
        die "cannot select Wine's null graphics driver"
    # A crash writes its backtrace to the log instead of waiting at a dialog.
    "$WINE" reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f >>"$LOG" 2>&1
    wineserver -w
}

# A prefix only for winepath and makensis.exe.
init_prefix "$WORK/wine-tools"

# Path for makensis.
nsis_path() {
    if [[ "$MAKENSIS_WINE" == 1 ]]; then winepath -w "$1"; else echo "$1"; fi
}

# build_installer <scripts dir> <version> <app stage> <tools stage or ""> <output>
build_installer() {
    local scripts=$1 version=$2 app=$3 tools=$4 out=$5
    local name
    name=$(basename "$out" .exe)
    local wine_paths=OFF
    [[ "$MAKENSIS_WINE" == 1 ]] && wine_paths=ON
    cmake "-DSTAGE=$app" "-DOUTPUT=$WORK/$name-app.nsh" -DMACRO=OPENMM2_APP "-DWINE_PATHS=$wine_paths" \
        -P "$scripts/GenerateFileList.cmake" >>"$LOG" 2>&1 || return 1
    local defines=("-DVERSION=$version" "-DSTAGE_DIR=$(nsis_path "$app")"
        "-DAPP_FILES_NSH=$(nsis_path "$WORK/$name-app.nsh")"
        "-DSOURCE_DIR=$(nsis_path "$SOURCE_DIR")" "-DOUTFILE=$(nsis_path "$out")")
    if [[ -n "$tools" ]]; then
        cmake "-DSTAGE=$tools" "-DOUTPUT=$WORK/$name-tools.nsh" -DMACRO=OPENMM2_TOOLS "-DWINE_PATHS=$wine_paths" \
            -P "$scripts/GenerateFileList.cmake" >>"$LOG" 2>&1 || return 1
        defines+=("-DTOOLS_FILES_NSH=$(nsis_path "$WORK/$name-tools.nsh")")
    fi
    local cmd=($MAKENSIS)
    [[ "$MAKENSIS_WINE" == 1 ]] && cmd=("$WINE" $MAKENSIS)
    # zlib instead of the script's solid LZMA: the same installer, built in a
    # fraction of the time.
    (cd "$WORK" && "${cmd[@]}" -V2 -INPUTCHARSET UTF8 "-XSetCompressor /FINAL zlib" "${defines[@]}" \
        "$(nsis_path "$scripts/openmm2.nsi")") >>"$LOG" 2>&1 || return 1
    [[ -f "$out" ]]
}

TOOLS_STAGE=""
[[ $HAVE_TOOLS == 1 ]] && TOOLS_STAGE=$STAGE/tools
SCRIPTS=$SOURCE_DIR/packaging/windows
SETUP_CURRENT=$WORK/setup-$CURRENT_VERSION.exe
SETUP_DUMMY=$WORK/setup-$DUMMY_VERSION.exe
SETUP_NEXT=$WORK/setup-$NEXT_VERSION.exe
SETUP_LEGACY=$WORK/setup-legacy-$LEGACY_VERSION.exe

check "build $CURRENT_VERSION" build_installer "$SCRIPTS" "$CURRENT_VERSION" "$STAGE/app" "$TOOLS_STAGE" "$SETUP_CURRENT"
# The same files plus a few that the next version drops.
rm -rf "$STAGE/app-dummy"
cp -r "$STAGE/app" "$STAGE/app-dummy"
echo "dropped in $NEXT_VERSION" >"$STAGE/app-dummy/obsolete.txt"
mkdir -p "$STAGE/app-dummy/obsolete-dir/nested"
echo "dropped in $NEXT_VERSION" >"$STAGE/app-dummy/obsolete-dir/nested/old.dat"
check "build $DUMMY_VERSION (with extra files)" \
    build_installer "$SCRIPTS" "$DUMMY_VERSION" "$STAGE/app-dummy" "$TOOLS_STAGE" "$SETUP_DUMMY"
check "build $NEXT_VERSION" build_installer "$SCRIPTS" "$NEXT_VERSION" "$STAGE/app" "$TOOLS_STAGE" "$SETUP_NEXT"

HAVE_LEGACY=0
if [[ "$LEGACY_REV" != none ]]; then
    # The release installers up to 0.4.1 used the same scripts; build that
    # one with today's program files plus the MSVC runtime DLLs the releases
    # shipped (stand-ins), which the MinGW build does not have.
    rm -rf "$WORK/legacy" "$STAGE/app-legacy"
    mkdir -p "$WORK/legacy"
    if git -C "$SOURCE_DIR" archive "$LEGACY_REV" packaging/windows 2>>"$LOG" | tar -x -C "$WORK/legacy"; then
        cp -r "$STAGE/app" "$STAGE/app-legacy"
        echo "stand-in" >"$STAGE/app-legacy/vcruntime140.dll"
        echo "stand-in" >"$STAGE/app-legacy/msvcp140.dll"
        check "build the $LEGACY_REV installer as $LEGACY_VERSION" build_installer \
            "$WORK/legacy/packaging/windows" "$LEGACY_VERSION" "$STAGE/app-legacy" "$TOOLS_STAGE" "$SETUP_LEGACY" &&
            HAVE_LEGACY=1
    else
        fail "git archive $LEGACY_REV (fetch the tag, or set LEGACY_REV=none)"
    fi
fi
if [[ $FAILURES -ne 0 ]]; then
    echo "Building failed; see $LOG" >&2
    exit 1
fi

# --- Helpers: test data, Wine ----------------------------------------------

# Synthetic game data: PKZIP archives (which OpenMM2 reads like the DAVE
# ones) with the two files --check-source looks for.
make_gamedata() { # <dir> <extra text for a bigger MM2CORE.AR, or "">
    local out=$1 extra=$2 tmp=$1.src
    rm -rf "$out" "$tmp"
    mkdir -p "$tmp/core/tune" "$tmp/core/city" "$tmp/tex/texture" "$tmp/aud/aud" "$out"
    echo "synthetic test data" >"$tmp/core/tune/vpbug.info"
    echo "synthetic test data" >"$tmp/core/city/london.psdl"
    [[ -n "$extra" ]] && echo "$extra" >"$tmp/core/tune/extra.txt"
    echo "synthetic test data" >"$tmp/tex/texture/dummy.tex"
    echo "synthetic test data" >"$tmp/aud/aud/dummy.wav"
    (cd "$tmp/core" && cmake -E tar cf "$out/MM2CORE.AR" --format=zip tune city) &&
        (cd "$tmp/tex" && cmake -E tar cf "$out/MM2TEX.AR" --format=zip texture) &&
        (cd "$tmp/aud" && cmake -E tar cf "$out/MM2AUD.AR" --format=zip aud) || die "making test data failed"
    rm -rf "$tmp"
}
make_gamedata "$WORK/data-v1" ""
make_gamedata "$WORK/data-v2" "this MM2CORE.AR differs in size from data-v1's"

# holder.exe: holds a file open the way Windows holds a running program's
# executable (others may read it, not write it), or, copied over
# openmm2.exe and started, is a running openmm2.exe itself.
cat >"$WORK/holder.c" <<'EOF'
// holder <file> <flag>   open <file> sharing only reads, wait while <flag> exists
// holder - <flag>        just wait while <flag> exists
// Creates <flag>.ready once it holds the file.
#include <stdio.h>
#include <wchar.h>
#include <windows.h>
int wmain(int argc, wchar_t** argv) {
    if (argc != 3)
        return 2;
    HANDLE h = INVALID_HANDLE_VALUE;
    if (wcscmp(argv[1], L"-") != 0) {
        h = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return 3;
    }
    wchar_t ready[MAX_PATH + 8];
    swprintf(ready, MAX_PATH + 8, L"%ls.ready", argv[2]);
    HANDLE r = CreateFileW(ready, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (r != INVALID_HANDLE_VALUE)
        CloseHandle(r);
    for (int i = 0; i < 3000 && GetFileAttributesW(argv[2]) != INVALID_FILE_ATTRIBUTES; ++i)
        Sleep(100);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    return 0;
}
EOF
"$MINGW_CC" -municode -static -O2 -o "$WORK/holder.exe" "$WORK/holder.c" >>"$LOG" 2>&1 || die "compiling holder.c failed"

# uidriver.exe: reads and clicks the wizard.
cat >"$WORK/uidriver.c" <<'EOF'
// uidriver: reads and clicks the OpenMM2 setup wizard (test-installer.sh).
//   uidriver dump [title]             the window title, then the visible
//                                     controls of the setup window (or of a
//                                     message box in front of it), one
//                                     "<control id>|<class>|<text>" line each;
//                                     line breaks in texts become spaces
//   uidriver click <id> [title]       clicks a button (if it is enabled)
//   uidriver uncheck <text> [title]   unchecks the check box with this text
// title is a prefix and defaults to "OpenMM2 Setup". Exit status 1 when there
// is no such window or button.
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <windows.h>

static void put(const wchar_t* s) {
    char buf[16384];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof buf, NULL, NULL);
    if (n > 0)
        fwrite(buf, 1, (size_t)n - 1, stdout);
}

static BOOL CALLBACK dumpChild(HWND w, LPARAM unused) {
    (void)unused;
    if (!IsWindowVisible(w))
        return TRUE;
    wchar_t cls[64], text[4096], line[4200];
    GetClassNameW(w, cls, 64);
    DWORD_PTR len = 0;
    text[0] = 0;
    if (!SendMessageTimeoutW(w, WM_GETTEXT, 4096, (LPARAM)text, SMTO_ABORTIFHUNG, 2000, &len))
        text[0] = 0;
    for (wchar_t* p = text; *p; ++p)
        if (*p == L'\r' || *p == L'\n')
            *p = L' ';
    swprintf(line, 4200, L"%d|%ls|%ls\n", GetDlgCtrlID(w), cls, text);
    put(line);
    return TRUE;
}

// The frontmost visible dialog whose title starts with g_prefix: setup's
// window ("OpenMM2 Setup", "OpenMM2 Setup: License Agreement", ...) or a
// message box in front of it.
static const wchar_t* g_prefix;
static HWND g_top;
static BOOL CALLBACK findTop(HWND w, LPARAM unused) {
    (void)unused;
    wchar_t cls[64], text[256];
    GetClassNameW(w, cls, 64);
    GetWindowTextW(w, text, 256);
    if (IsWindowVisible(w) && wcscmp(cls, L"#32770") == 0 && wcsncmp(text, g_prefix, wcslen(g_prefix)) == 0) {
        g_top = w;
        return FALSE;
    }
    return TRUE;
}

static const wchar_t* g_match;
static HWND g_found;
static BOOL CALLBACK findText(HWND w, LPARAM unused) {
    (void)unused;
    wchar_t text[512];
    GetWindowTextW(w, text, 512);
    if (IsWindowVisible(w) && wcscmp(text, g_match) == 0) {
        g_found = w;
        return FALSE;
    }
    return TRUE;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2)
        return 2;
    const int dump = wcscmp(argv[1], L"dump") == 0;
    g_prefix = L"OpenMM2 Setup";
    if (dump && argc > 2)
        g_prefix = argv[2];
    else if (!dump && argc > 3)
        g_prefix = argv[3];
    g_top = NULL;
    EnumWindows(findTop, 0);
    HWND w = g_top;
    if (!w)
        return 1;
    if (dump) {
        wchar_t title[256], line[300];
        GetWindowTextW(w, title, 256);
        swprintf(line, 300, L"title|%ls\n", title);
        put(line);
        EnumChildWindows(w, dumpChild, 0);
        return 0;
    }
    if (wcscmp(argv[1], L"click") == 0 && argc > 2) {
        // Posted, not sent: a page's code (nsDialogs::Show) runs inside the
        // handler of the click that showed it.
        const int id = _wtoi(argv[2]);
        HWND b = GetDlgItem(w, id);
        if (!b || !IsWindowEnabled(b))
            return 1;
        PostMessageW(w, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)b);
        return 0;
    }
    if (wcscmp(argv[1], L"uncheck") == 0 && argc > 2) {
        g_match = argv[2];
        g_found = NULL;
        EnumChildWindows(w, findText, 0);
        if (!g_found)
            return 1;
        DWORD_PTR r;
        SendMessageTimeoutW(g_found, BM_SETCHECK, BST_UNCHECKED, 0, SMTO_ABORTIFHUNG, 5000, &r);
        return 0;
    }
    return 2;
}
EOF
"$MINGW_CC" -municode -static -O2 -o "$WORK/uidriver.exe" "$WORK/uidriver.c" >>"$LOG" 2>&1 ||
    die "compiling uidriver.c failed"

# new_prefix <name>: a fresh Wine prefix for one scenario.
new_prefix() {
    init_prefix "$WORK/wine-$1"
    C=$WINEPREFIX/drive_c
    INSTDIR=$C/Program\ Files/OpenMM2
    INSTDIR_WIN='C:\Program Files\OpenMM2'
    START_LNK=$C/ProgramData/Microsoft/Windows/Start\ Menu/Programs/OpenMM2.lnk
    DESKTOP_LNK=$C/users/Public/Desktop/OpenMM2.lnk
}

# setup <installer> <arguments...>: runs it and prints its exit code. (Setup
# waits for everything it starts; wineserver -w would also wait for holder.exe.)
setup() {
    local exe=$1 code=0
    shift
    echo "--- $(basename "$exe") $*" >>"$LOG"
    timeout "$TIMEOUT" "$WINE" "$exe" "$@" >>"$LOG" 2>&1 || code=$?
    echo "$code"
}

# uninstall <install dir (Unix)>: the uninstaller restarts itself from %TEMP%,
# so wait for Wine rather than for the process.
uninstall() {
    echo "--- uninstall $1" >>"$LOG"
    timeout "$TIMEOUT" "$WINE" "$(winepath -w "$1/uninstall.exe")" /S >>"$LOG" 2>&1
    timeout "$TIMEOUT" wineserver -w
}

# reg_value <key> <value>: the data of a registry value, "" if missing.
reg_value() {
    "$WINE" reg query "$1" /v "$2" /reg:64 2>/dev/null | tr -d '\r' |
        sed -n "s/^ *$2 *REG_[A-Z_]* *//p"
}
reg_key_exists() {
    "$WINE" reg query "$1" /reg:64 >/dev/null 2>&1
}
check_no_key() { # <description> <key>
    if reg_key_exists "$2"; then fail "$1"; else pass "$1"; fi
}
REG_KEY='HKLM\Software\OpenMM2'
UNINST_KEY='HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenMM2'

# Files under <dir> (relative, sorted), without gamedata\.
installed_files() {
    (cd "$1" && find . -type f ! -path './gamedata/*' | sed 's|^\./||' | LC_ALL=C sort)
}
# The files a component stage holds.
stage_files() {
    (cd "$1" && find . -type f | sed 's|^\./||')
}
# expected_files <app stage> <with tools 0/1> <extra names...>
expected_files() {
    local app=$1 tools=$2
    shift 2
    {
        stage_files "$app"
        [[ $tools == 1 && $HAVE_TOOLS == 1 ]] && stage_files "$STAGE/tools"
        printf '%s\n' "$@"
    } | LC_ALL=C sort
}
# The manifest's entries as Unix paths (sorted).
manifest_entries() {
    grep -v '^;' "$1/install-manifest.txt" | tr -d '\r' | tr '\\' '/' | grep -v '^$' | LC_ALL=C sort
}
# check_installed <description> <install dir> <app stage> <tools 0/1> [ini]
# The files are exactly the program, the uninstaller, the manifest (and the
# game data setting), and the manifest lists exactly what setup copied.
check_installed() {
    local what=$1 dir=$2 app=$3 tools=$4 ini=${5:-}
    expected_files "$app" "$tools" uninstall.exe install-manifest.txt $ini >"$WORK/expected.txt"
    installed_files "$dir" >"$WORK/actual.txt"
    check_same_lines "$what: installed files" "$WORK/actual.txt" "$WORK/expected.txt"
    expected_files "$app" "$tools" uninstall.exe >"$WORK/expected.txt"
    if [[ -f "$dir/install-manifest.txt" ]]; then
        manifest_entries "$dir" >"$WORK/actual.txt"
        check_same_lines "$what: install-manifest.txt" "$WORK/actual.txt" "$WORK/expected.txt"
    else
        fail "$what: install-manifest.txt is missing"
    fi
}
# The names directly in a folder, sorted, on one line.
top_level() {
    (cd "$1" && find . -mindepth 1 -maxdepth 1 | sed 's|^\./||' | LC_ALL=C sort | tr '\n' ' ' | sed 's/ $//')
}
ini_source() {
    tr -d '\r' <"$1/openmm2-install.ini" 2>/dev/null | sed -n 's/^Source=//p'
}
# A snapshot of a folder (names, sizes, contents) to show nothing changed.
snapshot() {
    (cd "$1" 2>/dev/null && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r md5sum)
}
state() { # files and registry
    snapshot "$INSTDIR"
    reg_value "$REG_KEY" Version
    reg_value "$REG_KEY" InstallDir
}

# hold <file (Windows path) or -> <exe>: starts holder.exe and waits until it
# holds; release stops it.
HOLD_PID=""
hold() {
    HOLD_FLAG=$WORK/hold-flag
    rm -f "$HOLD_FLAG" "$HOLD_FLAG.ready"
    touch "$HOLD_FLAG"
    "$WINE" "$2" "$1" "$(winepath -w "$HOLD_FLAG")" >>"$LOG" 2>&1 &
    HOLD_PID=$!
    local i
    for ((i = 0; i < 300; ++i)); do
        [[ -f "$HOLD_FLAG.ready" ]] && return 0
        sleep 0.1
    done
    return 1
}
release() {
    rm -f "$HOLD_FLAG"
    wait "$HOLD_PID" 2>/dev/null
    HOLD_PID=""
}

DATA_V1=$(winepath -w "$WORK/data-v1")
DATA_V2=$(winepath -w "$WORK/data-v2")

# --- Scenarios -----------------------------------------------------------------
# Each starts from a fresh Wine prefix. SCENARIOS (space separated) picks some.

scenario_new() {
    section "New installation ($CURRENT_VERSION, silent, with /SOURCE and /COPYDATA)"
    new_prefix new
    check_eq "exit code" "$(setup "$SETUP_CURRENT" /S "/SOURCE=$DATA_V1" /COPYDATA)" 0
    check_installed "new" "$INSTDIR" "$STAGE/app" 0 openmm2-install.ini
    check "game data copied" cmp "$WORK/data-v1/MM2CORE.AR" "$INSTDIR/gamedata/MM2CORE.AR"
    check_eq "openmm2-install.ini Source" "$(ini_source "$INSTDIR")" "$INSTDIR_WIN\\gamedata"
    check_eq "registry Version" "$(reg_value "$REG_KEY" Version)" "$CURRENT_VERSION"
    check_eq "registry InstallDir" "$(reg_value "$REG_KEY" InstallDir)" "$INSTDIR_WIN"
    check_eq "registry StartMenuShortcut" "$(reg_value "$REG_KEY" StartMenuShortcut)" 0x1
    check_eq "registry DesktopShortcut" "$(reg_value "$REG_KEY" DesktopShortcut)" 0x1
    [[ $HAVE_TOOLS == 1 ]] && check_eq "registry Tools" "$(reg_value "$REG_KEY" Tools)" 0x0
    check_eq "uninstall entry DisplayVersion" "$(reg_value "$UNINST_KEY" DisplayVersion)" "$CURRENT_VERSION"
    check "Start menu shortcut" test -f "$START_LNK"
    check "desktop shortcut" test -f "$DESKTOP_LNK"

    section "Uninstall (silent: keeps the game data copy)"
    uninstall "$INSTDIR"
    check_eq "left in the install folder" "$(top_level "$INSTDIR")" "gamedata"
    check_no_key "registry key removed" "$REG_KEY"
    check_no_key "uninstall entry removed" "$UNINST_KEY"
    check "Start menu shortcut removed" test ! -e "$START_LNK"
    check "desktop shortcut removed" test ! -e "$DESKTOP_LNK"
}

# One installation through updates, repair, downgrades, a running game, a
# failed copy and a damaged manifest, then uninstalled.
scenario_updates() {
    local tools_now=0
    if [[ $HAVE_LEGACY == 1 ]]; then
        section "Update from $LEGACY_VERSION ($LEGACY_REV installer: no manifest, nothing remembered)"
        new_prefix updates
        check_eq "install $LEGACY_VERSION" "$(setup "$SETUP_LEGACY" /S "/SOURCE=$DATA_V1" /COPYDATA)" 0
        check "$LEGACY_VERSION has the MSVC runtime stand-ins" test -f "$INSTDIR/vcruntime140.dll"
        # The user had the tools installed and deleted the desktop shortcut.
        if [[ $HAVE_TOOLS == 1 ]]; then cp "$STAGE/tools/mm2tool.exe" "$INSTDIR/"; fi
        tools_now=1
        rm -f "$DESKTOP_LNK"
        snapshot "$INSTDIR/gamedata" >"$WORK/gamedata-before.txt"
        local ini_before
        ini_before=$(ini_source "$INSTDIR")
        check_eq "update to $CURRENT_VERSION" "$(setup "$SETUP_CURRENT" /S)" 0
        check_installed "update" "$INSTDIR" "$STAGE/app" $tools_now openmm2-install.ini
        check "MSVC runtime stand-ins removed" test ! -e "$INSTDIR/vcruntime140.dll" -a ! -e "$INSTDIR/msvcp140.dll"
        snapshot "$INSTDIR/gamedata" >"$WORK/gamedata-after.txt"
        check_same_lines "game data untouched" "$WORK/gamedata-after.txt" "$WORK/gamedata-before.txt"
        check_eq "openmm2-install.ini kept" "$(ini_source "$INSTDIR")" "$ini_before"
        check_eq "registry Version" "$(reg_value "$REG_KEY" Version)" "$CURRENT_VERSION"
        check "Start menu shortcut kept" test -f "$START_LNK"
        check "deleted desktop shortcut not re-created" test ! -e "$DESKTOP_LNK"
        check_eq "registry StartMenuShortcut" "$(reg_value "$REG_KEY" StartMenuShortcut)" 0x1
        check_eq "registry DesktopShortcut" "$(reg_value "$REG_KEY" DesktopShortcut)" 0x0
        [[ $HAVE_TOOLS == 1 ]] &&
            check_eq "registry Tools (mm2tool.exe was there)" "$(reg_value "$REG_KEY" Tools)" 0x1
    else
        section "Update from a release installer: skipped (LEGACY_REV=$LEGACY_REV)"
        new_prefix updates
        check_eq "install $CURRENT_VERSION" "$(setup "$SETUP_CURRENT" /S "/SOURCE=$DATA_V1" /COPYDATA)" 0
    fi

    section "Update to $DUMMY_VERSION, then to $NEXT_VERSION, which no longer ships some files"
    check_eq "update to $DUMMY_VERSION" "$(setup "$SETUP_DUMMY" /S)" 0
    check_installed "$DUMMY_VERSION" "$INSTDIR" "$STAGE/app-dummy" $tools_now openmm2-install.ini
    check_eq "update to $NEXT_VERSION" "$(setup "$SETUP_NEXT" /S)" 0
    check_installed "$NEXT_VERSION" "$INSTDIR" "$STAGE/app" $tools_now openmm2-install.ini
    check "dropped folder removed" test ! -e "$INSTDIR/obsolete-dir"
    check_eq "registry Version" "$(reg_value "$REG_KEY" Version)" "$NEXT_VERSION"
    [[ $HAVE_LEGACY == 1 ]] && check "desktop shortcut still off" test ! -e "$DESKTOP_LNK"

    section "Repair: $NEXT_VERSION again"
    rm -f "$INSTDIR/README.md" "$INSTDIR/fonts/LiberationSans-Bold.ttf"
    echo "mine" >"$INSTDIR/notes.txt"
    echo "mine" >"$INSTDIR/fonts/my-font.ttf"
    check_eq "exit code (no downgrade question)" "$(setup "$SETUP_NEXT" /S)" 0
    check "deleted files restored" test -f "$INSTDIR/README.md" -a -f "$INSTDIR/fonts/LiberationSans-Bold.ttf"
    check "the user's own files kept" test -f "$INSTDIR/notes.txt" -a -f "$INSTDIR/fonts/my-font.ttf"
    rm -f "$INSTDIR/notes.txt" "$INSTDIR/fonts/my-font.ttf"
    check_installed "repair" "$INSTDIR" "$STAGE/app" $tools_now openmm2-install.ini

    section "Downgrade to $CURRENT_VERSION (silent)"
    state >"$WORK/state-before.txt"
    check_eq "without /ALLOWDOWNGRADE: exit code" "$(setup "$SETUP_CURRENT" /S)" 3
    state >"$WORK/state-after.txt"
    check_same_lines "without /ALLOWDOWNGRADE: nothing changed" "$WORK/state-after.txt" "$WORK/state-before.txt"
    check_eq "with /ALLOWDOWNGRADE: exit code" "$(setup "$SETUP_CURRENT" /S /ALLOWDOWNGRADE)" 0
    check_eq "registry Version" "$(reg_value "$REG_KEY" Version)" "$CURRENT_VERSION"
    check_installed "downgrade" "$INSTDIR" "$STAGE/app" $tools_now openmm2-install.ini

    section "openmm2.exe in use"
    state >"$WORK/state-before.txt"
    if hold "$INSTDIR_WIN\\openmm2.exe" "$WORK/holder.exe"; then
        check_eq "held open by another program: exit code" "$(setup "$SETUP_NEXT" /S)" 4
        state >"$WORK/state-after.txt"
        check_same_lines "held open: nothing changed" "$WORK/state-after.txt" "$WORK/state-before.txt"
        release
    else
        fail "holder.exe did not start"
    fi
    # The real case: openmm2.exe is the running program (a stand-in).
    cp "$WORK/holder.exe" "$INSTDIR/openmm2.exe"
    state >"$WORK/state-before.txt"
    if hold - "$INSTDIR_WIN\\openmm2.exe"; then
        check_eq "running: exit code" "$(setup "$SETUP_NEXT" /S)" 4
        state >"$WORK/state-after.txt"
        check_same_lines "running: nothing changed" "$WORK/state-after.txt" "$WORK/state-before.txt"
        # "_?=<dir>" runs the uninstaller in place instead of from a copy in
        # %TEMP%, so its exit code is visible. It must come last, unquoted:
        # use the 8.3 name of "Program Files".
        local short code=0
        short=$(winepath -s "$INSTDIR_WIN" 2>/dev/null | tr -d '\r')
        if [[ -n "$short" && "$short" != *" "* ]]; then
            timeout "$TIMEOUT" "$WINE" "$(winepath -w "$INSTDIR/uninstall.exe")" /S "_?=$short" >>"$LOG" 2>&1 ||
                code=$?
            check_eq "running: uninstaller exit code" "$code" 4
            state >"$WORK/state-after.txt"
            check_same_lines "running: the uninstaller removed nothing" "$WORK/state-after.txt" \
                "$WORK/state-before.txt"
        else
            fail "no 8.3 name for $INSTDIR_WIN"
        fi
        release
    else
        fail "openmm2.exe stand-in did not start"
    fi
    check_eq "closed: exit code" "$(setup "$SETUP_NEXT" /S)" 0
    check "openmm2.exe replaced" cmp "$STAGE/app/openmm2.exe" "$INSTDIR/openmm2.exe"
    check_installed "after closing" "$INSTDIR" "$STAGE/app" $tools_now openmm2-install.ini

    section "Another file held open by another program"
    # Not the game, so setup does not ask first; the file cannot be replaced,
    # which must fail the installation rather than leave the old file.
    if hold "$INSTDIR_WIN\\fonts\\LiberationSans-Bold.ttf" "$WORK/holder.exe"; then
        check_eq "exit code" "$(setup "$SETUP_NEXT" /S)" 1
        release
    else
        fail "holder.exe did not start"
    fi
    check_eq "released: exit code" "$(setup "$SETUP_NEXT" /S)" 0
    check_installed "after releasing it" "$INSTDIR" "$STAGE/app" $tools_now openmm2-install.ini

    section "Copying game data fails over an earlier copy"
    # data-v2's MM2CORE.AR differs in size from the copy in gamedata\, which
    # is made read-only, so --import-source fails when it would replace it,
    # after copying (here: skipping) the archives before it.
    rm -rf "$WORK/gamedata-probe"
    cp -r "$INSTDIR/gamedata" "$WORK/gamedata-probe"
    chmod a-w "$WORK/gamedata-probe/MM2CORE.AR"
    local code=0
    timeout "$TIMEOUT" "$WINE" "$INSTDIR/openmm2.exe" --import-source "$DATA_V2" \
        "$(winepath -w "$WORK/gamedata-probe")" >>"$LOG" 2>&1 || code=$?
    check "the simulated failure makes --import-source fail" test "$code" -ne 0
    chmod -R u+w "$WORK/gamedata-probe"
    rm -rf "$WORK/gamedata-probe"
    snapshot "$INSTDIR/gamedata" >"$WORK/gamedata-before.txt"
    chmod a-w "$INSTDIR/gamedata/MM2CORE.AR"
    check_eq "exit code (installed; the failed copy is reported, not fatal)" \
        "$(setup "$SETUP_NEXT" /S "/SOURCE=$DATA_V2" /COPYDATA)" 0
    chmod u+w "$INSTDIR/gamedata/MM2CORE.AR"
    snapshot "$INSTDIR/gamedata" >"$WORK/gamedata-after.txt"
    check_same_lines "earlier copy untouched, no partial files" "$WORK/gamedata-after.txt" \
        "$WORK/gamedata-before.txt"
    check_eq "openmm2-install.ini keeps the earlier copy" "$(ini_source "$INSTDIR")" "$INSTDIR_WIN\\gamedata"

    section "A damaged install-manifest.txt cannot remove anything else"
    mkdir -p "$C/Program Files/Bystander"
    echo "not OpenMM2's" >"$C/Program Files/Bystander/keep.txt"
    echo "not OpenMM2's" >"$C/keep.txt"
    cat >>"$INSTDIR/install-manifest.txt" <<'EOF'
..\Bystander\keep.txt
C:\keep.txt
\keep.txt
gamedata\MM2CORE.AR
GAMEDATA\MM2TEX.AR
.\gamedata\MM2AUD.AR
gamedata.\MM2AUD.AR
GAMEDA~1\MM2AUD.AR
gamedata
*.*
openmm2-install.ini
EOF
    snapshot "$INSTDIR/gamedata" >"$WORK/gamedata-before.txt"
    check_eq "repair: exit code" "$(setup "$SETUP_NEXT" /S)" 0
    check "files outside the install folder kept" test -f "$C/Program Files/Bystander/keep.txt" -a -f "$C/keep.txt"
    snapshot "$INSTDIR/gamedata" >"$WORK/gamedata-after.txt"
    check_same_lines "game data kept" "$WORK/gamedata-after.txt" "$WORK/gamedata-before.txt"
    check "openmm2-install.ini kept" test -f "$INSTDIR/openmm2-install.ini"
    check_installed "after the damaged manifest" "$INSTDIR" "$STAGE/app" $tools_now openmm2-install.ini

    section "Uninstall after the updates"
    uninstall "$INSTDIR"
    check_eq "left in the install folder (the game data copy)" "$(top_level "$INSTDIR")" "gamedata"
    check_no_key "registry key removed" "$REG_KEY"
    check_no_key "uninstall entry removed" "$UNINST_KEY"
    check "Start menu shortcut removed" test ! -e "$START_LNK"
}

scenario_clean() {
    section "Updates without game data, then uninstall: nothing left"
    new_prefix clean
    if [[ $HAVE_LEGACY == 1 ]]; then
        check_eq "install $LEGACY_VERSION" "$(setup "$SETUP_LEGACY" /S)" 0
    else
        check_eq "install $CURRENT_VERSION" "$(setup "$SETUP_CURRENT" /S)" 0
    fi
    check_eq "update to $DUMMY_VERSION" "$(setup "$SETUP_DUMMY" /S)" 0
    check_eq "update to $NEXT_VERSION" "$(setup "$SETUP_NEXT" /S)" 0
    check_installed "$NEXT_VERSION" "$INSTDIR" "$STAGE/app" 0
    uninstall "$INSTDIR"
    check "install folder removed" test ! -e "$INSTDIR"
    check_no_key "registry key removed" "$REG_KEY"
    check_no_key "uninstall entry removed" "$UNINST_KEY"
    check "shortcuts removed" test ! -e "$START_LNK" -a ! -e "$DESKTOP_LNK"
}

scenario_move() {
    section "Installing to another folder with /D= moves the installation"
    new_prefix move
    check_eq "install $CURRENT_VERSION" "$(setup "$SETUP_CURRENT" /S "/SOURCE=$DATA_V1" /COPYDATA)" 0
    local old=$INSTDIR new=$C/Games/OpenMM2
    check_eq "update to $NEXT_VERSION with /D=C:\\Games\\OpenMM2" "$(setup "$SETUP_NEXT" /S '/D=C:\Games\OpenMM2')" 0
    check_installed "new folder" "$new" "$STAGE/app" 0 openmm2-install.ini
    check_eq "new openmm2-install.ini uses the old folder's game data" "$(ini_source "$new")" \
        "$INSTDIR_WIN\\gamedata"
    check_eq "old folder keeps only the game data and its setting" "$(top_level "$old")" \
        "gamedata openmm2-install.ini"
    check_eq "registry InstallDir" "$(reg_value "$REG_KEY" InstallDir)" 'C:\Games\OpenMM2'
    check_eq "uninstall entry InstallLocation" "$(reg_value "$UNINST_KEY" InstallLocation)" 'C:\Games\OpenMM2'
    check "Start menu shortcut" test -f "$START_LNK"
    uninstall "$new"
    check "new folder removed" test ! -e "$new"
    check_no_key "registry key removed" "$REG_KEY"
    check "old folder's game data is still there" test -f "$old/gamedata/MM2CORE.AR"
}

# The wizard, driven through uidriver.exe: which pages an installation, an
# update, a repair and a downgrade show, what they say, and the questions
# about a newer version and a running game.
ui() {
    timeout 30 "$WINE" "$WORK/uidriver.exe" "$@" 2>>"$LOG"
}
# wait_for <text> [seconds]: waits until the wizard (or a message box in front
# of it) shows a line containing <text>; leaves its texts in $WORK/page.txt.
wait_for() {
    local text=$1 limit=${2:-60}
    local end=$((SECONDS + limit))
    while ((SECONDS < end)); do
        if ui dump >"$WORK/page.txt" && grep -Fq -- "$text" "$WORK/page.txt"; then
            {
                echo "--- saw: $text"
                cat "$WORK/page.txt"
            } >>"$LOG"
            return 0
        fi
        sleep 0.2
    done
    {
        echo "--- did not see: $text; last texts:"
        cat "$WORK/page.txt"
    } >>"$LOG"
    return 1
}
# shown <text>: the last texts contain <text>.
shown() {
    grep -Fq -- "$1" "$WORK/page.txt"
}
wizard_start() { # <installer> [arguments...]
    echo "--- $(basename "$1") (wizard) ${*:2}" >>"$LOG"
    timeout "$TIMEOUT" "$WINE" "$@" >>"$LOG" 2>&1 &
    WIZARD_PID=$!
}
# wizard_exit: waits for setup to end; its exit code goes to $WIZARD_CODE
# (not printed: $(...) would run "wait" in a subshell).
wizard_exit() {
    WIZARD_CODE=0
    wait "$WIZARD_PID" || WIZARD_CODE=$?
}
# From the instfiles page to the end; the game is not started. (The wizard
# moves on to the finish page by itself once the installation is complete.)
wizard_finish() {
    check "installation complete: finish page" wait_for "Completing OpenMM2 Setup" 120
    ui uncheck "Start OpenMM2"
    ui click 1
    wizard_exit
    check_eq "exit code" "$WIZARD_CODE" 0
}
# The game data page shows its header before it has extracted the program
# it checks the choice with; wait for its own controls.
GAME_DATA_PAGE="|&Copy the game data to this computer"
# Cancel and confirm.
wizard_cancel() {
    ui click 2
    wait_for "Are you sure you want to quit" 30 && ui click 6
    wizard_exit
    check_eq "cancelled: exit code" "$WIZARD_CODE" 1
}

scenario_wizard() {
    section "Wizard: new installation"
    new_prefix wizard
    wizard_start "$SETUP_CURRENT" "/SOURCE=$DATA_V1"
    check "welcome page" wait_for "This will install OpenMM2 $CURRENT_VERSION, an open source"
    ui click 1
    check "licence page" wait_for "1037|Static|License Agreement"
    ui click 1
    check "components page" wait_for "1037|Static|Choose Components"
    ui click 1
    check "game data page" wait_for "$GAME_DATA_PAGE"
    check "  pre-filled from /SOURCE" shown "|$DATA_V1"
    check "  button: Next" shown "1|Button|&Next >"
    ui click 1
    check "directory page" wait_for "1037|Static|Choose Install Location"
    check "  button: Install" shown "1|Button|&Install"
    ui click 1
    wizard_finish
    check_eq "registry Version" "$(reg_value "$REG_KEY" Version)" "$CURRENT_VERSION"
    check_eq "openmm2-install.ini Source" "$(ini_source "$INSTDIR")" "$DATA_V1"

    section "Wizard: update $CURRENT_VERSION -> $NEXT_VERSION (Next, Next, Install)"
    wizard_start "$SETUP_NEXT"
    check "welcome page" wait_for "This will update OpenMM2 from $CURRENT_VERSION to $NEXT_VERSION."
    check "  names the folder" shown "It stays in $INSTDIR_WIN."
    ui click 1
    check "components page (licence skipped)" wait_for "1037|Static|Choose Components"
    ui click 1
    check "game data page" wait_for "$GAME_DATA_PAGE"
    check "  pre-filled from the last installation" shown "|$DATA_V1"
    check "  button: Install (directory page skipped)" shown "1|Button|&Install"
    ui click 1
    wizard_finish
    check_eq "registry Version" "$(reg_value "$REG_KEY" Version)" "$NEXT_VERSION"
    check_installed "wizard update" "$INSTDIR" "$STAGE/app" 0 openmm2-install.ini

    section "Wizard: repair"
    wizard_start "$SETUP_NEXT"
    check "welcome page" wait_for "OpenMM2 $NEXT_VERSION is already installed. Setup will install it again"
    wizard_cancel

    section "Wizard: downgrade"
    wizard_start "$SETUP_CURRENT"
    check "question" wait_for "OpenMM2 $NEXT_VERSION is installed, which is newer than this setup ($CURRENT_VERSION)."
    ui click 7 # No
    wizard_exit
    check_eq "answered No: exit code" "$WIZARD_CODE" 1
    check_eq "  registry Version unchanged" "$(reg_value "$REG_KEY" Version)" "$NEXT_VERSION"
    wizard_start "$SETUP_CURRENT"
    check "question again" wait_for "is newer than this setup"
    ui click 6 # Yes
    check "welcome page" wait_for "This will replace OpenMM2 $NEXT_VERSION with the older version $CURRENT_VERSION."
    wizard_cancel

    section "Wizard: OpenMM2 running"
    if hold "$INSTDIR_WIN\\openmm2.exe" "$WORK/holder.exe"; then
        wizard_start "$SETUP_NEXT"
        check "welcome page" wait_for "1|Button|&Next >"
        ui click 1
        wait_for "1037|Static|Choose Components" && ui click 1
        wait_for "$GAME_DATA_PAGE" && ui click 1
        check "asks to close it" wait_for "OpenMM2 is running. Close it, then click Retry."
        check "  with Retry and Cancel" shown "4|Button|&Retry"
        release
        ui click 4 # Retry
        wizard_finish
    else
        fail "holder.exe did not start"
    fi
}

run_scenario() {
    case $1 in
    new | updates | clean | move | wizard) "scenario_$1" ;;
    *) die "unknown scenario '$1'" ;;
    esac
}
# A prefix takes a few hundred MB: keep only those of failed scenarios.
for scenario in ${SCENARIOS:-new updates clean move wizard}; do
    failures_before=$FAILURES
    run_scenario "$scenario"
    if [[ $FAILURES -eq $failures_before ]]; then
        wineserver -k 2>/dev/null
        rm -rf "$WINEPREFIX"
    fi
done
export WINEPREFIX=$WORK/wine-tools
wineserver -k 2>/dev/null
[[ $FAILURES -eq 0 ]] && rm -rf "$WINEPREFIX"

echo
if [[ $FAILURES -eq 0 ]]; then
    echo "All $CHECKS checks passed (work directory: $WORK)."
else
    echo "$FAILURES of $CHECKS checks failed; log: $LOG"
fi
[[ $FAILURES -eq 0 ]]
