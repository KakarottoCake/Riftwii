#!/bin/bash
# Runs riftwii.dol in Dolphin with the project's isolated user directory
# (build-dolphin/user), captures USB Gecko output to build-dolphin/gecko.log
# and Dolphin's own log to build-dolphin/user/Logs/dolphin.log, then kills
# Dolphin after $1 seconds (default 60).
#   DOLPHIN_DIR=/c/path/to/Dolphin-x64 tools/dolphin/run.sh 45
# DOL=/path/to/other.dol runs that file instead of ./riftwii.dol. Only the
# Dolphin started here is stopped: another Dolphin window stays open.
# The user directory's Config/Logger.ini selects what dolphin.log holds
# (Verbosity = 4 is INFO; BOOT, IOS, IOS_DI, IOS_ES, IOS_SD, DVD, OSREPORT
# and EXI on). For a hang inside the game set Verbosity = 5 and
# PowerPC = True: exceptions are logged with their address
# (`W[PowerPC]: ISI exception at 0x935d0020` found section 23 of
# docs/CONDUCTOR_REVIEW_2.md). A run that must sync files back to
# build-dolphin/user/Load/WiiSDSync must end without booting a game.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SECS="${1:-60}"
DOLPHIN="${DOLPHIN_DIR:?set DOLPHIN_DIR to the folder holding Dolphin.exe}/Dolphin.exe"
USER_DIR_WIN="$(cygpath -m "$ROOT/build-dolphin/user" 2>/dev/null || echo "$ROOT/build-dolphin/user")"
DOL_PATH="${DOL:-$ROOT/riftwii.dol}"
DOL_WIN="$(cygpath -m "$DOL_PATH" 2>/dev/null || echo "$DOL_PATH")"
rm -f "$ROOT/build-dolphin/gecko.log" "$ROOT/build-dolphin/user/Logs/dolphin.log"
python "$ROOT/tools/dolphin/gecko_log.py" "$ROOT/build-dolphin/gecko.log" "$SECS" > /dev/null 2>&1 &
GECKO_PID=$!
"$DOLPHIN" -u "$USER_DIR_WIN" -e "$DOL_WIN" -b > "$ROOT/build-dolphin/dolphin_stdout.txt" 2>&1 &
DOLPHIN_PID=$!
sleep "$SECS"
# Ours is the one whose command line names this user directory.
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='Dolphin.exe'\" | Where-Object { \$_.CommandLine -like '*$USER_DIR_WIN*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" > /dev/null 2>&1
kill $GECKO_PID 2>/dev/null
wait $DOLPHIN_PID 2>/dev/null
echo "=== gecko.log ==="
cat "$ROOT/build-dolphin/gecko.log" 2>/dev/null
echo
echo "=== dolphin.log (DI/ES/BOOT lines) ==="
grep -E "IOS_DI|IOS_ES|BOOT|OSREPORT|Starting IOS|E\[" "$ROOT/build-dolphin/user/Logs/dolphin.log" 2>/dev/null | grep -vE "IOS_SD" | head -80
