#!/usr/bin/env bash
# Builds Hollow on a Mac: a universal (Apple Silicon + Intel) VST3, AU and standalone app.
#
# Needs: Xcode command line tools (`xcode-select --install`) and CMake (`brew install cmake`).
# JUCE 9 is fetched on first run. Output goes to dist/mac and is installed to ~/Library/Audio/Plug-Ins.
#
#   scripts/build-mac.sh                build (universal), sign for this Mac, install, run the DSP tests + auval
#   ARCHS=arm64 scripts/build-mac.sh    Apple Silicon only (M1/M2/M3/M4): about twice as fast to build
#   INSTALL=0 scripts/build-mac.sh      build only
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${CONFIG:-Release}"
BUILD="${BUILD_DIR:-$ROOT/build/mac-$(echo "$CONFIG" | tr "[:upper:]" "[:lower:]")}"
JUCE_DIR="${JUCE_DIR:-$HOME/toolchains/JUCE-9.0.2}"

if [ ! -d "$JUCE_DIR" ]; then
    mkdir -p "$(dirname "$JUCE_DIR")"
    git clone -q --depth 1 --branch 9.0.2 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
fi

cmake -S "$ROOT" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_OSX_ARCHITECTURES="${ARCHS:-arm64;x86_64}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DJUCE_DIR="$JUCE_DIR"

cmake --build "$BUILD" --config "$CONFIG" --parallel "$(sysctl -n hw.ncpu)"

ART="$BUILD/Hollow_artefacts/$CONFIG"
DIST="$ROOT/dist/mac"
rm -rf "$DIST"
mkdir -p "$DIST"
cp -R "$ART/VST3/Hollow.vst3" "$ART/AU/Hollow.component" "$ART/Standalone/Hollow.app" "$DIST/"

# Apple Silicon refuses unsigned code: an ad-hoc signature is enough on the machine that built it.
# (To give it to other people: sign with a Developer ID and notarize, see README.)
for bundle in "$DIST"/Hollow.vst3 "$DIST"/Hollow.component "$DIST"/Hollow.app; do
    codesign --force --deep --sign - "$bundle"
done

lipo -info "$DIST/Hollow.vst3/Contents/MacOS/Hollow"

bash "$ROOT/scripts/test-dsp.sh"

if [ "${INSTALL:-1}" = "1" ]; then
    mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
    rm -rf ~/Library/Audio/Plug-Ins/VST3/Hollow.vst3 ~/Library/Audio/Plug-Ins/Components/Hollow.component
    cp -R "$DIST/Hollow.vst3" ~/Library/Audio/Plug-Ins/VST3/
    cp -R "$DIST/Hollow.component" ~/Library/Audio/Plug-Ins/Components/
    killall -9 AudioComponentRegistrar 2>/dev/null || true # make macOS rescan AUs
    auval -v aufx Hlw1 Lhei | tail -3
fi

echo "Built: $DIST"
