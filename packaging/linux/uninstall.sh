#!/usr/bin/env bash
# Removes what install.sh installed. Use --system for a system-wide install (run with sudo).
set -euo pipefail
if [ "${1:-}" = "--system" ]; then
  BIN=/usr/bin; VST3=/usr/lib/vst3; CLAP=/usr/lib/clap
  APPS=/usr/share/applications; ICONS=/usr/share/icons/hicolor/256x256/apps
else
  BIN="$HOME/.local/bin"; VST3="$HOME/.vst3"; CLAP="$HOME/.clap"
  APPS="$HOME/.local/share/applications"; ICONS="$HOME/.local/share/icons/hicolor/256x256/apps"
fi
rm -f "$BIN/reverseback" "$CLAP/ReverseBack.clap" "$APPS/reverseback.desktop" "$ICONS/reverseback.png"
rm -rf "$VST3/ReverseBack.vst3"
echo "ReverseBack removed. Your settings in ~/.config/ReverseBack were left in place."
