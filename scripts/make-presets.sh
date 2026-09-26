#!/usr/bin/env bash
# Checks and writes the preset pack (tools/PresetPack.cpp) into presets/, through the real processor on Windows.
# Run scripts/build-windows.sh first.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WIN_TEMP_WSL="$(wslpath "$(cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')")/hollow-pack"
WIN_TEMP="$(wslpath -w "$WIN_TEMP_WSL")"

rm -rf "$WIN_TEMP_WSL"
mkdir -p "$WIN_TEMP_WSL"
cp "$ROOT/dist/HollowSnapshot.exe" "$WIN_TEMP_WSL/"
cd "$WIN_TEMP_WSL"

./HollowSnapshot.exe --write-pack "$WIN_TEMP\\presets" | tr -d '\r'

rm -rf "$ROOT/presets"
cp -r presets "$ROOT/presets"
find "$ROOT/presets" -name '*.hollowpreset' -exec sed -i 's/\r$//' {} + # the repository keeps LF line endings
echo "$(find "$ROOT/presets" -name '*.hollowpreset' | wc -l) presets in $ROOT/presets"
