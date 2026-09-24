#!/usr/bin/env bash
# One-time setup of everything needed to cross-compile Windows VST3s from Linux/WSL without sudo.
# Installs into $TOOLCHAIN_ROOT (default ~/toolchains):
#   cmake/, bin/ninja, llvm/ (clang-cl, lld-link, llvm-rc...), xwin/ (MSVC CRT + Windows SDK),
#   JUCE-9.0.2/, hostsys/ (Linux dev headers for JUCE's host tool juceaide), pluginval/
#
# NOTE: `xwin --accept-license` accepts Microsoft's license terms for the MSVC CRT and Windows SDK
# (the same terms as installing Visual Studio Build Tools).
set -euo pipefail

TC="${TOOLCHAIN_ROOT:-$HOME/toolchains}"
CMAKE_VER=4.4.3
NINJA_VER=1.13.2
LLVM_VER=23.1.2
XWIN_VER=0.10.0
JUCE_VER=9.0.2
PLUGINVAL_VER=1.0.4

mkdir -p "$TC/dl" "$TC/bin"
cd "$TC"

fetch() { [ -f "dl/$1" ] || curl -fsSL -o "dl/$1" "$2"; }

if [ ! -x cmake/bin/cmake ]; then
    fetch cmake.tgz "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-linux-x86_64.tar.gz"
    tar xzf dl/cmake.tgz && mv "cmake-$CMAKE_VER-linux-x86_64" cmake
fi

if [ ! -x bin/ninja ]; then
    fetch ninja.zip "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VER/ninja-linux.zip"
    python3 -c "import zipfile; zipfile.ZipFile('dl/ninja.zip').extractall('bin')" && chmod +x bin/ninja
fi

if [ ! -x llvm/bin/clang-cl ]; then
    fetch llvm.tar.xz "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VER/LLVM-$LLVM_VER-Linux-X64.tar.xz"
    tar xJf dl/llvm.tar.xz && mv "LLVM-$LLVM_VER-Linux-X64" llvm
fi

# The LLVM release links lld-link/llvm-mt against ICU 70 (Ubuntu 22.04); drop it next to LLVM if missing
if ldd llvm/bin/lld-link | grep -q "not found"; then
    fetch libicu70.deb "http://archive.ubuntu.com/ubuntu/pool/main/i/icu/libicu70_70.1-2ubuntu1_amd64.deb"
    mkdir -p llvm-deps && dpkg-deb -x dl/libicu70.deb llvm-deps
    cp -a llvm-deps/usr/lib/x86_64-linux-gnu/libicu*.so.70* llvm/lib/
fi

if [ ! -x bin/xwin ]; then
    fetch xwin.tgz "https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VER/xwin-$XWIN_VER-x86_64-unknown-linux-musl.tar.gz"
    tar xzf dl/xwin.tgz && mv "xwin-$XWIN_VER-x86_64-unknown-linux-musl/xwin" bin/ && rm -rf "xwin-$XWIN_VER-x86_64-unknown-linux-musl"
fi

if [ ! -d xwin/sdk ]; then
    bin/xwin --accept-license --arch x86_64 --variant desktop splat --output "$TC/xwin" --include-debug-libs
fi

# JUCE uses mixed-case #pragma comment(lib, ...) names; the SDK on a case-sensitive FS needs aliases
if [ ! -d "JUCE-$JUCE_VER" ]; then
    git clone -q --depth 1 --branch "$JUCE_VER" https://github.com/juce-framework/JUCE.git "JUCE-$JUCE_VER"
fi

grep -rhoE 'pragma comment\s*\(\s*lib\s*,\s*"[^"]+"' "JUCE-$JUCE_VER/modules" | sed -E 's/.*"([^"]+)"/\1/' | sort -u |
while read -r name; do
    case "$name" in *.lib|*.Lib) ;; *) name="$name.lib" ;; esac
    for d in xwin/sdk/lib/um/x86_64 xwin/sdk/lib/ucrt/x86_64 xwin/crt/lib/x86_64; do
        [ -e "$d/$name" ] && break
        match=$(ls "$d" | grep -ix "$name" | head -1 || true)
        if [ -n "$match" ]; then ln -s "$match" "$d/$name"; break; fi
    done
done

# juceaide (JUCE's build helper) is compiled for the host and needs freetype/fontconfig/X11 headers
if [ ! -x hostsys/root/usr/bin/pkgconf ]; then
    mkdir -p hostsys/debs hostsys/root
    (cd hostsys/debs && apt-get download libfreetype-dev libfontconfig-dev libx11-dev libxext-dev libxrandr-dev \
        libxinerama-dev libxcursor-dev libxrender-dev libxcomposite-dev libxi-dev x11proto-dev libxfixes-dev \
        pkgconf-bin libpkgconf7 zlib1g-dev libpng-dev libbrotli-dev libbz2-dev libexpat1-dev uuid-dev)
    for d in hostsys/debs/*.deb; do dpkg-deb -x "$d" hostsys/root; done

    for l in hostsys/root/usr/lib/x86_64-linux-gnu/*.so; do
        t=$(readlink "$l")
        [ -e "hostsys/root/usr/lib/x86_64-linux-gnu/$t" ] || ln -sf "/usr/lib/x86_64-linux-gnu/$t" "$l"
    done
fi

cat > bin/pkg-config <<'WRAPPER'
#!/bin/sh
# pkgconf wrapper resolving .pc files from the locally extracted dev packages (no sudo needed)
R="$HOME/toolchains/hostsys/root"
export LD_LIBRARY_PATH="$R/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PKG_CONFIG_LIBDIR="$R/usr/lib/x86_64-linux-gnu/pkgconfig:$R/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$R"
exec "$R/usr/bin/pkgconf" "$@"
WRAPPER
chmod +x bin/pkg-config

if [ ! -f pluginval/pluginval.exe ]; then
    fetch pluginval.zip "https://github.com/Tracktion/pluginval/releases/download/v$PLUGINVAL_VER/pluginval_Windows.zip"
    mkdir -p pluginval && python3 -c "import zipfile; zipfile.ZipFile('dl/pluginval.zip').extractall('pluginval')"
fi

echo "Toolchain ready in $TC"
