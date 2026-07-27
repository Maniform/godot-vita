#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

cpu_count() {
  local count
  if command -v nproc >/dev/null 2>&1; then
    nproc
  elif count=$(sysctl -n hw.logicalcpu 2>/dev/null) && [[ -n $count ]]; then
    echo "$count"
  elif count=$(getconf _NPROCESSORS_ONLN 2>/dev/null) && [[ -n $count ]]; then
    echo "$count"
  else
    echo 1
  fi
}

JOBS=${JOBS:-$(cpu_count)}
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

build_macos_arch() {
  local arch=$1
  [[ $(uname -s) == Darwin ]] || {
    echo "macOS builds require a macOS host and the Xcode command-line tools." >&2
    exit 1
  }
  xcrun --sdk macosx --show-sdk-path >/dev/null
  scons platform=osx target=release_debug tools=yes arch="$arch" bits=64 debug_symbols=no lto=none -j"$JOBS"
}

build_macos_universal() {
  build_macos_arch arm64
  build_macos_arch x86_64
  xcrun lipo -create \
    bin/godot.osx.opt.tools.arm64 \
    bin/godot.osx.opt.tools.x86_64 \
    -output bin/godot.osx.opt.tools.universal
  chmod +x bin/godot.osx.opt.tools.universal
}

case "$TARGET" in
  all)
    if [[ $(uname -s) == Darwin ]]; then
      build_macos_universal
    else
      build_windows_x64
      build_windows_arm64
      build_linux
    fi
    ;;
  windows-x64) build_windows_x64 ;;
  windows-arm64) build_windows_arm64 ;;
  linux|linux-x64) build_linux ;;
  macos)
    case "$(uname -m)" in
      arm64|aarch64) build_macos_arch arm64 ;;
      x86_64) build_macos_arch x86_64 ;;
      *) echo "Unsupported macOS architecture: $(uname -m)" >&2; exit 1 ;;
    esac
    ;;
  macos-arm64) build_macos_arch arm64 ;;
  macos-x64) build_macos_arch x86_64 ;;
  macos-universal) build_macos_universal ;;
  *) echo "Usage: $0 [all|windows-x64|windows-arm64|linux|macos|macos-x64|macos-arm64|macos-universal]" >&2; exit 2 ;;
esac
