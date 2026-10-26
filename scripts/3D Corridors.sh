#!/bin/bash
#
# 3D Corridors - dArkOSen Tools launcher
# Runs as a child of EmulationStation, on top of it.
#
# Layout:
#   <this folder>/3D Corridors.sh
#   <this folder>/3D_Corridors/3D_Corridors
#
# Settings (edit the values below):
#   SPEED       --speed       0.5 - 1.5, 0.1 steps   walk speed multiplier              default 1.0
#   BRIGHTNESS  --brightness  0 - 100                panel light output, 0 = 25%        default 100
#   AMBIENT     --ambient     0 - 100                light between panels, 0 = 10%      default 100
#   PANELS      --panels      1 - 5                  x dead panels, 8% per step         default 1
#   FLICKER     --flicker     1 - 5                  x flickering panels, 4% per step   default 1
#   FOG         --fog         1 - 5                  x fog density                      default 1
#   VOLUME      --volume      0 - 5                  0 off, 3 = 0 dB, 3 dB per step     default 3
#
# Controls:
#   d-pad left / right   turn 90 degrees
#   anything else        exit back to EmulationStation
#
# Log: 3D_Corridors.log in this folder

SPEED=1.0
BRIGHTNESS=50
AMBIENT=25
PANELS=2
FLICKER=3
FOG=1
VOLUME=3

DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$DIR/3D_Corridors/3D_Corridors"
LOG="$DIR/3D_Corridors.log"

{
    printf '%-14s %s\n' "Started" "$(date '+%Y-%m-%d %H:%M:%S')"
    printf '%-14s %s\n' "Binary"  "$BIN"
} > "$LOG"

if [ ! -f "$BIN" ]; then
    printf '%-14s %s\n' "ERROR" "binary not found" >> "$LOG"
    exit 1
fi

if [ ! -x "$BIN" ]; then
    chmod +x "$BIN" 2>> "$LOG" || {
        printf '%-14s %s\n' "ERROR" "binary not executable" >> "$LOG"
        exit 1
    }
fi

"$BIN" \
    --speed      "$SPEED" \
    --ambient    "$AMBIENT" \
    --panels     "$PANELS" \
    --flicker    "$FLICKER" \
    --fog        "$FOG" \
    --volume     "$VOLUME" \
    --brightness "$BRIGHTNESS" \
    >> "$LOG" 2>&1
rc=$?

{
    printf '%-14s %s\n' "Exit code" "$rc"
    printf '%-14s %s\n' "Stopped"   "$(date '+%Y-%m-%d %H:%M:%S')"
} >> "$LOG"

exit "$rc"