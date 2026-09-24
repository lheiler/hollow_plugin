#!/usr/bin/env bash
# Cross-compiles the Windows x64 VST3 (+ standalone app and test tool) from Linux/WSL.
# Requires the toolchain from scripts/setup-toolchain.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="${TOOLCHAIN_ROOT:-$HOME/toolchains}"
CONFIG="${CONFIG:-Release}"
BUILD="${BUILD_DIR:-$ROOT/build/windows-${CONFIG,,}}"

# ninja, cmake and the pkg-config wrapper used by JUCE's host-side helper (juceaide)
export PATH="$TC/bin:$TC/cmake/bin:$PATH"

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/WindowsCrossClangCl.cmake" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DJUCE_DIR="${JUCE_DIR:-$TC/JUCE-9.0.2}"

cmake --build "$BUILD" --parallel "${JOBS:-$(nproc)}"

ART="$BUILD/Hollow_artefacts/$CONFIG"
DIST="$ROOT/dist"
rm -rf "$DIST/Hollow.vst3"
mkdir -p "$DIST"
cp -r "$ART/VST3/Hollow.vst3" "$DIST/"
cp "$ART/Standalone/Hollow.exe" "$DIST/"

if [ -f "$BUILD/HollowSnapshot_artefacts/$CONFIG/HollowSnapshot.exe" ]; then
    cp "$BUILD/HollowSnapshot_artefacts/$CONFIG/HollowSnapshot.exe" "$DIST/"
fi

echo
echo "Built:"
find "$DIST" -maxdepth 3 -type f \( -name '*.vst3' -o -name '*.exe' \) -exec ls -la {} \;
