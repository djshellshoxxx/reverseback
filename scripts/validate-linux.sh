#!/usr/bin/env bash
# Release checks for the Linux artifacts. Usage: scripts/validate-linux.sh <build-dir> [pluginval-binary]
# Exit code 0 only if every check that could run passed; checks that could not run are reported as SKIPPED.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$(cd "${1:?build dir}" && pwd)"
PLUGINVAL="${2:-$(command -v pluginval || true)}"
ART="$BUILD/ReverseBack_artefacts/Release"
APP="$ART/Standalone/ReverseBack"; VST3="$ART/VST3/ReverseBack.vst3"; CLAP="$ART/CLAP/ReverseBack.clap"
FAIL=0
ok()   { echo "  ok       $*"; }
bad()  { echo "  FAILED   $*"; FAIL=1; }
skip() { echo "  SKIPPED  $*"; }

echo "[1] linked libraries (nothing missing; only the documented system packages)"
for f in "$APP" "$CLAP" "$VST3/Contents/x86_64-linux/ReverseBack.so"; do
  if ldd "$f" | grep -q "not found"; then bad "$f has missing libraries"; ldd "$f" | grep "not found"; else ok "$(basename "$f")"; fi
  ldd "$f" | awk '{print $1}' | grep -E "^lib" | grep -viE "^(libc|libm|libdl|libpthread|librt|libstdc\+\+|libgcc_s|libasound|libfreetype|libfontconfig|libX|libGL|libz|libpng|libbrotli|libbz2|libexpat|libuuid|libxcb|libXau|libXdmcp|libffi|libGLdispatch|libGLX|libharfbuzz|libgraphite|libglib|libpcre|libbsd|libmd|libwayland|ld-linux|libpthread)" | sed 's/^/             unexpected: /'
done

echo "[2] exported symbols of the plugin binaries (format entry points only)"
for f in "$CLAP" "$VST3/Contents/x86_64-linux/ReverseBack.so"; do
  syms=$(nm -D --defined-only "$f" | awk '{print $3}' | grep -vE "^(_init|_fini|__bss_start|_edata|_end)$" | sort | tr '\n' ' ')
  n=$(echo $syms | wc -w)
  if [ "$n" -le 4 ]; then ok "$(basename "$f"): exports $syms"; else bad "$(basename "$f"): $n exported symbols (visibility leak)"; fi
done

echo "[3] standalone self-test (offline engine check, no display or audio device)"
if env -u DISPLAY "$APP" --selftest 2>/dev/null | grep -q "selftest: PASS"; then ok "--selftest PASS"; else bad "--selftest"; fi
"$APP" --version 2>/dev/null | grep -q "ReverseBack" && ok "--version" || bad "--version"

echo "[4] CLAP probe (entry, descriptor, parameters, ports, state, processing)"
CLAPINC="$(find "$BUILD/_deps" /home/user/deps -type d -path '*clap-libs/clap/include' 2>/dev/null | head -1)"
[ -z "$CLAPINC" ] && CLAPINC="$(find "$BUILD" -type d -path '*clap/include' 2>/dev/null | head -1)"
if [ -n "$CLAPINC" ] && command -v g++ >/dev/null; then
  g++ -std=c++17 "$ROOT/scripts/clap_probe.cpp" -I"$CLAPINC" -ldl -o "$BUILD/clap_probe" 2>/dev/null \
    && { if command -v xvfb-run >/dev/null; then xvfb-run -a "$BUILD/clap_probe" "$CLAP" 2>&1 | grep -v "^ALSA\|^jack\|^Jack" | sed 's/^/     /'; else "$BUILD/clap_probe" "$CLAP" | sed 's/^/     /'; fi; [ "${PIPESTATUS[0]}" = 0 ] && ok "clap_probe" || bad "clap_probe"; } \
    || bad "could not compile clap_probe"
else skip "clap headers or g++ not found"; fi

echo "[5] VST3 validation (pluginval, strictness 5)"
if [ -n "$PLUGINVAL" ] && [ -x "$PLUGINVAL" ]; then
  if command -v xvfb-run >/dev/null; then RUN="xvfb-run -a"; else RUN=""; fi
  if $RUN "$PLUGINVAL" --strictness-level 5 --validate-in-process --timeout-ms 120000 "$VST3" > "$BUILD/pluginval.log" 2>&1; then ok "pluginval passed (log: $BUILD/pluginval.log)"; else bad "pluginval failed, see $BUILD/pluginval.log"; tail -25 "$BUILD/pluginval.log"; fi
else skip "pluginval not found (pass its path as the second argument)"; fi

echo "[6] desktop entry"
if command -v desktop-file-validate >/dev/null; then desktop-file-validate "$ROOT/packaging/linux/reverseback.desktop" && ok "desktop-file-validate" || bad "desktop-file-validate"; else skip "desktop-file-validate not installed"; fi

echo
[ "$FAIL" = 0 ] && echo "ALL CHECKS THAT COULD RUN PASSED" || echo "SOME CHECKS FAILED"
exit "$FAIL"
