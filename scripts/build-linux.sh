#!/usr/bin/env bash
# One-shot Linux build of ReverseBack (Standalone + VST3 + CLAP) from a fresh checkout.
# Usage: scripts/build-linux.sh [--no-apt] [--install]
#   --no-apt    skip installing system packages (you already have them)
#   --install   run the per-user installer when the build succeeds
# Needs: Ubuntu/Debian-like system with sudo (for apt), internet (to fetch JUCE and CLAP at pinned versions).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APT=1; INSTALL=0
for a in "$@"; do case "$a" in --no-apt) APT=0 ;; --install) INSTALL=1 ;; esac; done

if [ "$APT" = 1 ] && command -v apt-get >/dev/null 2>&1; then
  echo "==> Installing build dependencies (sudo)"
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends build-essential cmake ninja-build pkg-config git ca-certificates \
    libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev \
    libfreetype-dev libfontconfig1-dev libgl1-mesa-dev libcurl4-openssl-dev libjack-jackd2-dev zip dpkg-dev xvfb
fi

echo "==> Configuring"
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
echo "==> Building (the first build compiles JUCE and takes several minutes)"
cmake --build "$ROOT/build" --target ReverseBack_Standalone ReverseBack_VST3 ReverseBack_CLAP rb_core_tests rb_integration_tests -j"$(nproc)"

echo "==> Testing"
"$ROOT/build/rb_core_tests" | tail -2
if command -v xvfb-run >/dev/null 2>&1; then xvfb-run -a "$ROOT/build/rb_integration_tests" | tail -2; fi
"$ROOT/build/ReverseBack_artefacts/Release/Standalone/ReverseBack" --selftest

echo "==> Packaging"
VERSION="$(sed -n 's/^project(ReverseBack VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")-beta.1"
"$ROOT/scripts/package-linux.sh" "$ROOT/build" "$VERSION" "$ROOT/dist"

if [ "$INSTALL" = 1 ]; then
  tar -xzf "$ROOT/dist/ReverseBack-$VERSION-linux-x86_64.tar.gz" -C "$ROOT/dist"
  "$ROOT/dist/ReverseBack-$VERSION-linux-x86_64/install.sh"
fi
echo "Done. Artifacts are in $ROOT/dist"
