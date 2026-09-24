# Cross-compile Windows x64 binaries from Linux/WSL using clang-cl + lld-link and the
# MSVC CRT / Windows SDK fetched by `xwin splat` (see scripts/setup-toolchain.sh).
#
#   TOOLCHAIN_ROOT (env or cache) defaults to ~/toolchains and must contain:
#     llvm/   - an LLVM release (clang-cl, lld-link, llvm-rc, llvm-lib)
#     xwin/   - output of `xwin splat` (crt/ and sdk/)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT TOOLCHAIN_ROOT)
    if(DEFINED ENV{TOOLCHAIN_ROOT})
        set(TOOLCHAIN_ROOT "$ENV{TOOLCHAIN_ROOT}")
    else()
        set(TOOLCHAIN_ROOT "$ENV{HOME}/toolchains")
    endif()
endif()
set(TOOLCHAIN_ROOT "${TOOLCHAIN_ROOT}" CACHE PATH "Root of the cross toolchain" FORCE)

set(LLVM_BIN "${TOOLCHAIN_ROOT}/llvm/bin")
set(XWIN "${TOOLCHAIN_ROOT}/xwin")

set(CMAKE_C_COMPILER   "${LLVM_BIN}/clang-cl" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${LLVM_BIN}/clang-cl" CACHE FILEPATH "")
set(CMAKE_LINKER       "${LLVM_BIN}/lld-link" CACHE FILEPATH "")
set(CMAKE_AR           "${LLVM_BIN}/llvm-lib" CACHE FILEPATH "")
set(CMAKE_RC_COMPILER  "${LLVM_BIN}/llvm-rc" CACHE FILEPATH "")
set(CMAKE_MT           "${LLVM_BIN}/llvm-mt" CACHE FILEPATH "")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(_xwin_includes
    "${XWIN}/crt/include"
    "${XWIN}/sdk/include/ucrt"
    "${XWIN}/sdk/include/um"
    "${XWIN}/sdk/include/shared"
    "${XWIN}/sdk/include/winrt"
    "${XWIN}/sdk/include/cppwinrt")

set(_xwin_cflags "--target=x86_64-pc-windows-msvc -fms-compatibility-version=19.40 /EHsc")
foreach(dir IN LISTS _xwin_includes)
    string(APPEND _xwin_cflags " /imsvc \"${dir}\"")
endforeach()

set(CMAKE_C_FLAGS_INIT   "${_xwin_cflags}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_cflags}")
set(CMAKE_RC_FLAGS_INIT  "-I \"${XWIN}/sdk/include/um\" -I \"${XWIN}/sdk/include/shared\" -I \"${XWIN}/crt/include\" -I \"${XWIN}/sdk/include/ucrt\"")

set(_xwin_ldflags "/libpath:\"${XWIN}/crt/lib/x86_64\" /libpath:\"${XWIN}/sdk/lib/um/x86_64\" /libpath:\"${XWIN}/sdk/lib/ucrt/x86_64\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_xwin_ldflags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_ldflags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_ldflags}")

set(CMAKE_FIND_ROOT_PATH "${XWIN}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
