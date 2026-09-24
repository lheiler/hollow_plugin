#!/usr/bin/env bash
# Builds and runs the native DSP unit tests (no JUCE needed).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
${CXX:-g++} -std=c++20 -O2 -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Wno-float-conversion \
    -Isource tests/DspTests.cpp -o build/tests/dsp_tests
./build/tests/dsp_tests
