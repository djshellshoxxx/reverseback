#!/usr/bin/env bash
# Launches the standalone under Xvfb and captures screenshots of every mode and sheet.
# Usage: scripts/gui-shots.sh <path-to-ReverseBack-binary> <output-dir> [audio-file-for-file-mode]
set -euo pipefail
BIN="${1:?binary}"; OUT="${2:?output dir}"; AUDIO="${3:-}"
mkdir -p "$OUT"
export HOME="$(mktemp -d)"            # clean settings: first-run defaults
export DISPLAY=:98
Xvfb :98 -screen 0 1100x800x24 >/dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null || true; pkill -f "$BIN" 2>/dev/null || true' EXIT
sleep 1.5
"$BIN" >/dev/null 2>&1 &
sleep 4
WIN=$(xdotool search --name ReverseBack | head -1)
eval "$(xdotool getwindowgeometry --shell "$WIN")"   # X Y WIDTH HEIGHT
click() { xdotool mousemove $((X + $1)) $((Y + $2)) click 1; sleep "${3:-0.7}"; }
shot()  { import -window root "$OUT/$1.png"; }

shot 01-record-ready
click 480 92;  shot 02-live-ready                 # Live tab
click 779 92;  shot 03-file-empty                 # File tab
click 181 92                                       # back to Record
click 875 638; shot 04-record-advanced             # Advanced drawer
click 875 638
click 915 32;  shot 05-settings                    # gear
xdotool key Escape; sleep 0.5
click 845 32;  shot 06-menu                        # menu popup
xdotool key Escape; sleep 0.5
click 760 32;  shot 07-presets                     # presets popup
xdotool key Escape; sleep 0.5
if [ -n "$AUDIO" ]; then
  click 779 92
  xdotool key ctrl+o; sleep 1.5; shot 08-open-dialog; xdotool key Escape; sleep 0.5
fi
echo "screenshots in $OUT"
