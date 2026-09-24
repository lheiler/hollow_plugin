#!/usr/bin/env bash
# Runs the headless harness and pluginval on Windows through WSL interop.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="${TOOLCHAIN_ROOT:-$HOME/toolchains}"
WIN_TEMP_WSL="$(wslpath "$(cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')")/hollow-test"
WIN_TEMP="$(wslpath -w "$WIN_TEMP_WSL")"

rm -rf "$WIN_TEMP_WSL/Hollow.vst3"
mkdir -p "$WIN_TEMP_WSL"
cp -r "$ROOT/dist/Hollow.vst3" "$ROOT/dist/HollowSnapshot.exe" "$TC/pluginval/pluginval.exe" "$WIN_TEMP_WSL/"
cd "$WIN_TEMP_WSL"

echo "== Harness (render, automation, state, bypass, screenshots) =="
./HollowSnapshot.exe "$WIN_TEMP\\shots" | tr -d '\r'
mkdir -p "$ROOT/build/screenshots" && cp shots/*.png "$ROOT/build/screenshots/"

echo "== pluginval (strictness 10, no GUI windows) =="
./pluginval.exe --strictness-level 10 --skip-gui-tests --validate-in-process --validate "$WIN_TEMP\\Hollow.vst3" | tr -d '\r' | tail -3
