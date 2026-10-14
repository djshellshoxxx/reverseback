#!/usr/bin/env bash
# Builds the Linux beta artifacts from an existing CMake build directory.
# Usage: scripts/package-linux.sh <build-dir> <version> <output-dir>
#   e.g. scripts/package-linux.sh build 0.1.0-beta.1 dist
set -euo pipefail
BUILD="$(cd "${1:?build dir}" && pwd)"
VERSION="${2:?version}"
OUT="$(mkdir -p "${3:?output dir}" && cd "$3" && pwd)"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ART="$BUILD/ReverseBack_artefacts/Release"
ARCH=x86_64
NAME="ReverseBack-$VERSION-linux-$ARCH"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

[ -x "$ART/Standalone/ReverseBack" ] || { echo "missing standalone binary in $ART"; exit 1; }
[ -d "$ART/VST3/ReverseBack.vst3" ] || { echo "missing VST3 bundle in $ART"; exit 1; }
[ -f "$ART/CLAP/ReverseBack.clap" ] || { echo "missing CLAP in $ART"; exit 1; }

COMMIT="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
D="$STAGE/$NAME"
mkdir -p "$D"
cp "$ART/Standalone/ReverseBack" "$D/ReverseBack"
cp -r "$ART/VST3/ReverseBack.vst3" "$D/"
cp "$ART/CLAP/ReverseBack.clap" "$D/"
# smaller binaries; symbols are not needed by users (rebuild from source for debugging)
strip --strip-unneeded "$D/ReverseBack" "$D/ReverseBack.clap" "$D/ReverseBack.vst3/Contents/x86_64-linux/ReverseBack.so"
cp "$ROOT/packaging/linux/"{install.sh,uninstall.sh,reverseback.desktop,README-linux.txt} "$D/"
cp "$ROOT/Assets/icon-256.png" "$D/reverseback.png"
cp "$ROOT/"{LICENSE,COPYRIGHT-TRADEMARK.md,THIRD_PARTY_LICENSES.md} "$D/"
cp "$ROOT/Assets/Inter-LICENSE.txt" "$D/"
cp -r "$ROOT/LICENSES" "$D/"
cat > "$D/SOURCE.txt" <<EOT
ReverseBack $VERSION
Complete corresponding source (AGPL-3.0 section 6): https://github.com/djshellshoxxx/reverseback
Commit: $COMMIT
Dependencies are fetched at the pinned versions listed in cmake/Plugin.cmake
(JUCE 8.0.15, clap-juce-extensions 7adee3a1bd4684d4caa5601100e364abccff4b4d).
Build instructions: spec/BUILD_RELEASE.md and scripts/build-linux.sh
EOT
chmod +x "$D/install.sh" "$D/uninstall.sh"

# 1. everything
tar -C "$STAGE" -czf "$OUT/$NAME.tar.gz" "$NAME"

# 2. single-format archives
( cd "$STAGE" && mkdir -p vst3 clap standalone
  cp -r "$D/ReverseBack.vst3" vst3/ && cp "$D/"{LICENSE,SOURCE.txt,THIRD_PARTY_LICENSES.md} vst3/ && cp -r "$D/LICENSES" vst3/
  cp "$D/ReverseBack.clap" clap/ && cp "$D/"{LICENSE,SOURCE.txt,THIRD_PARTY_LICENSES.md} clap/ && cp -r "$D/LICENSES" clap/
  cp "$D/"{ReverseBack,reverseback.desktop,reverseback.png,LICENSE,SOURCE.txt,THIRD_PARTY_LICENSES.md} standalone/ && cp -r "$D/LICENSES" standalone/
  ( cd vst3 && zip -qr "$OUT/ReverseBack-$VERSION-vst3-linux-$ARCH.zip" . )
  ( cd clap && zip -qr "$OUT/ReverseBack-$VERSION-clap-linux-$ARCH.zip" . )
  tar -C standalone -czf "$OUT/ReverseBack-$VERSION-standalone-linux-$ARCH.tar.gz" . )

# 3. Debian package
DEBVER="$(printf '%s' "$VERSION" | sed 's/-/~/')"   # 0.1.0-beta.1 -> 0.1.0~beta.1 (sorts before 0.1.0)
P="$STAGE/deb"
mkdir -p "$P/DEBIAN" "$P/usr/bin" "$P/usr/lib/vst3" "$P/usr/lib/clap" "$P/usr/share/applications" \
         "$P/usr/share/icons/hicolor/256x256/apps" "$P/usr/share/doc/reverseback"
install -m 0755 "$D/ReverseBack" "$P/usr/bin/reverseback"
cp -r "$D/ReverseBack.vst3" "$P/usr/lib/vst3/"
install -m 0755 "$D/ReverseBack.clap" "$P/usr/lib/clap/ReverseBack.clap"
install -m 0644 "$D/reverseback.desktop" "$P/usr/share/applications/"
install -m 0644 "$D/reverseback.png" "$P/usr/share/icons/hicolor/256x256/apps/"
cp "$D/"{LICENSE,SOURCE.txt,THIRD_PARTY_LICENSES.md,Inter-LICENSE.txt} "$P/usr/share/doc/reverseback/"
cp "$ROOT/LICENSES/AGPL-3.0.txt" "$P/usr/share/doc/reverseback/"
cat > "$P/usr/share/doc/reverseback/copyright" <<EOT
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: ReverseBack
Source: https://github.com/djshellshoxxx/reverseback

Files: *
Copyright: 2026 Sheldon Davidson
License: AGPL-3.0-or-later
 The ReverseBack source is MIT licensed; these binaries link JUCE 8 under its AGPLv3 option and are
 therefore distributed under the GNU Affero General Public License, version 3 or later. See
 THIRD_PARTY_LICENSES.md and SOURCE.txt in this directory. The Inter font is licensed under the SIL OFL 1.1.
EOT
SIZE_KB="$(du -sk "$P/usr" | cut -f1)"
cat > "$P/DEBIAN/control" <<EOT
Package: reverseback
Version: $DEBVER
Section: sound
Priority: optional
Architecture: amd64
Installed-Size: $SIZE_KB
Depends: libasound2t64 | libasound2, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1, libfreetype6, libfontconfig1, libgl1
Recommends: libjack-jackd2-0 | libjack-0.125
Maintainer: Circuit Drift Labs <djshellshoxxx@users.noreply.github.com>
Homepage: https://github.com/djshellshoxxx/reverseback
Description: playful audio reverser (standalone, VST3 and CLAP)
 ReverseBack records your voice, reverses live audio chunks or opens audio files and plays them backwards.
 Includes the standalone application, a VST3 plugin and a CLAP plugin.
EOT
dpkg-deb --root-owner-group --build "$P" "$OUT/reverseback_${DEBVER}_amd64.deb" >/dev/null

( cd "$OUT" && sha256sum "$NAME.tar.gz" "ReverseBack-$VERSION-vst3-linux-$ARCH.zip" "ReverseBack-$VERSION-clap-linux-$ARCH.zip" \
    "ReverseBack-$VERSION-standalone-linux-$ARCH.tar.gz" "reverseback_${DEBVER}_amd64.deb" > SHA256SUMS )
echo "Artifacts in $OUT:"; ls -la "$OUT"
