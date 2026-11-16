#!/usr/bin/env bash
# Installs ReverseBack from this folder.
#   ./install.sh            per-user install (~/.local, ~/.vst3, ~/.clap) - no root needed
#   sudo ./install.sh --system   system-wide (/usr/bin, /usr/lib/vst3, /usr/lib/clap)
# Needs no network access. Safe to run again.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

if [ "${1:-}" = "--system" ]; then
  BIN=/usr/bin; VST3=/usr/lib/vst3; CLAP=/usr/lib/clap
  APPS=/usr/share/applications; ICONS=/usr/share/icons/hicolor/256x256/apps
else
  BIN="$HOME/.local/bin"; VST3="$HOME/.vst3"; CLAP="$HOME/.clap"
  APPS="$HOME/.local/share/applications"; ICONS="$HOME/.local/share/icons/hicolor/256x256/apps"
fi

echo "Installing ReverseBack ..."
install -d "$BIN" "$VST3" "$CLAP" "$APPS" "$ICONS"
install -m 0755 "$HERE/ReverseBack" "$BIN/reverseback"
rm -rf "$VST3/ReverseBack.vst3"
cp -r "$HERE/ReverseBack.vst3" "$VST3/"
install -m 0755 "$HERE/ReverseBack.clap" "$CLAP/ReverseBack.clap"
# absolute Exec path: ~/.local/bin is not on every desktop session's PATH
sed "s|^Exec=reverseback |Exec=\"$BIN/reverseback\" |" "$HERE/reverseback.desktop" > "$APPS/reverseback.desktop"
chmod 0644 "$APPS/reverseback.desktop"
install -m 0644 "$HERE/reverseback.png" "$ICONS/reverseback.png"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1 || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "$(dirname "$(dirname "$ICONS")")" >/dev/null 2>&1 || true

echo
echo "  Standalone : $BIN/reverseback"
echo "  VST3       : $VST3/ReverseBack.vst3"
echo "  CLAP       : $CLAP/ReverseBack.clap"
case ":$PATH:" in *":$BIN:"*) ;; *) echo; echo "Note: $BIN is not on your PATH; add it or launch the app from your menu." ;; esac
echo
echo "Rescan plugins in your DAW to see ReverseBack. Done."
