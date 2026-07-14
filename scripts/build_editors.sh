#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
JOBS=${JOBS:-$(nproc)}
export SCONS_CACHE="${SCONS_CACHE:-$ROOT/.scons_cache}"
export SCONS_CACHE_LIMIT="${SCONS_CACHE_LIMIT:-10240}"
TARGET=${1:-all}
ARM64_PREFIX=${MINGW_ARM64_PREFIX:-$ROOT/.toolchains/llvm-mingw/bin/aarch64-w64-mingw32-}
X64_LLVM_PREFIX=${MINGW_X64_LLVM_PREFIX:-$ROOT/.toolchains/llvm-mingw/bin/x86_64-w64-mingw32-}

build_windows_x64() {
  if [[ $(uname -m) == aarch64 || $(uname -m) == arm64 ]]; then
    test -x "${X64_LLVM_PREFIX}clang++" || { echo "LLVM-MinGW x64 not found: ${X64_LLVM_PREFIX}clang++" >&2; exit 1; }
    scons platform=windows target=release_debug tools=yes arch=x86_64 bits=64 use_mingw=yes use_llvm=yes mingw_prefix_64="$X64_LLVM_PREFIX" debug_symbols=no lto=none -j"$JOBS"
  else
    scons platform=windows target=release_debug tools=yes bits=64 use_mingw=yes debug_symbols=no lto=none -j"$JOBS"
  fi
}

build_windows_arm64() {
  test -x "${ARM64_PREFIX}clang++" || { echo "LLVM-MinGW ARM64 not found: ${ARM64_PREFIX}clang++" >&2; exit 1; }
  MINGW_ARM64_PREFIX="$ARM64_PREFIX" scons platform=windows target=release_debug tools=yes arch=arm64 bits=64 use_mingw=yes use_llvm=yes mingw_prefix_arm64="$ARM64_PREFIX" target_win_version=0x0A00 debug_symbols=no lto=none -j"$JOBS"
}

build_linux() {
  scons platform=x11 target=release_debug tools=yes bits=64 debug_symbols=no lto=none -j"$JOBS"
}

case "$TARGET" in
  all) build_windows_x64; build_windows_arm64; build_linux ;;
  windows-x64) build_windows_x64 ;;
  windows-arm64) build_windows_arm64 ;;
  linux|linux-x64) build_linux ;;
  *) echo "Usage: $0 [all|windows-x64|windows-arm64|linux]" >&2; exit 2 ;;
esac
