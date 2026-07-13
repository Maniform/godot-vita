#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
JOBS=${JOBS:-$(nproc)}
export SCONS_CACHE="${SCONS_CACHE:-$ROOT/.scons_cache}"
export SCONS_CACHE_LIMIT="${SCONS_CACHE_LIMIT:-10240}"
TARGET=${1:-all}
OUT=${TEMPLATE_OUTPUT_DIR:-$ROOT/bin/export-templates}
STAGE="$OUT/staging"
ARM64_PREFIX=${MINGW_ARM64_PREFIX:-$ROOT/.toolchains/llvm-mingw/bin/aarch64-w64-mingw32-}
X64_LLVM_PREFIX=${MINGW_X64_LLVM_PREFIX:-$ROOT/.toolchains/llvm-mingw/bin/x86_64-w64-mingw32-}
VITAGL=${VITAGL:-no}
VITA_PVR_SDK=${VITA_PVR_SDK:-${VITASDK:-/usr/local/vitasdk}/arm-vita-eabi}
mkdir -p "$STAGE"

build_windows_x64() {
  local compiler_args=()
  if [[ $(uname -m) == aarch64 || $(uname -m) == arm64 ]]; then
    test -x "${X64_LLVM_PREFIX}clang++" || { echo "LLVM-MinGW x64 introuvable: ${X64_LLVM_PREFIX}clang++" >&2; exit 1; }
    compiler_args=(arch=x86_64 use_llvm=yes mingw_prefix_64="$X64_LLVM_PREFIX")
  fi
  scons platform=windows target=release tools=no bits=64 use_mingw=yes "${compiler_args[@]}" debug_symbols=no lto=none -j"$JOBS"
  cp bin/godot.windows.opt.64.exe "$STAGE/windows_64_release.exe"
  scons platform=windows target=release_debug tools=no bits=64 use_mingw=yes "${compiler_args[@]}" debug_symbols=no lto=none -j"$JOBS"
  cp bin/godot.windows.opt.debug.64.exe "$STAGE/windows_64_debug.exe"
}

build_windows_arm64() {
  test -x "${ARM64_PREFIX}clang++" || { echo "LLVM-MinGW ARM64 introuvable: ${ARM64_PREFIX}clang++" >&2; exit 1; }
  MINGW_ARM64_PREFIX="$ARM64_PREFIX" scons platform=windows target=release tools=no arch=arm64 bits=64 use_mingw=yes use_llvm=yes mingw_prefix_arm64="$ARM64_PREFIX" target_win_version=0x0A00 debug_symbols=no lto=none -j"$JOBS"
  cp bin/godot.windows.opt.arm64.exe "$STAGE/windows_arm64_release.exe"
  MINGW_ARM64_PREFIX="$ARM64_PREFIX" scons platform=windows target=release_debug tools=no arch=arm64 bits=64 use_mingw=yes use_llvm=yes mingw_prefix_arm64="$ARM64_PREFIX" target_win_version=0x0A00 debug_symbols=no lto=none -j"$JOBS"
  cp bin/godot.windows.opt.debug.arm64.exe "$STAGE/windows_arm64_debug.exe"
}

build_linux() {
  scons platform=x11 target=release tools=no bits=64 debug_symbols=no lto=none -j"$JOBS"
  cp bin/godot.x11.opt.64 "$STAGE/linux_x11_64_release"
  scons platform=x11 target=release_debug tools=no bits=64 debug_symbols=no lto=none -j"$JOBS"
  cp bin/godot.x11.opt.debug.64 "$STAGE/linux_x11_64_debug"
}

build_vita_variant() {
  local target=$1 name=$2
  local backend_args=(vitagl="$VITAGL")
  if [[ "$VITAGL" == no ]]; then
    backend_args+=(vita_pvr_sdk_path="$VITA_PVR_SDK")
  fi
  scons platform=vita target="$target" tools=no "${backend_args[@]}" debug_symbols=no lto=none -j"$JOBS" temp-build/eboot.bin
  python3 scripts/package_templates.py vita platform/vita/app temp-build/eboot.bin "$STAGE/$name"
}

build_vita() {
  build_vita_variant release vita_release.zip
  build_vita_variant release_debug vita_debug.zip
}

case "$TARGET" in
  all) build_windows_x64; build_windows_arm64; build_linux; build_vita ;;
  windows-x64) build_windows_x64 ;;
  windows-arm64) build_windows_arm64 ;;
  linux|linux-x64) build_linux ;;
  vita) build_vita ;;
  *) echo "Usage: $0 [all|windows-x64|windows-arm64|linux|vita]" >&2; exit 2 ;;
esac

mapfile -t FILES < <(find "$STAGE" -maxdepth 1 -type f -printf "%f=%p\n" | sort)
python3 scripts/package_templates.py bundle "$ROOT" "$OUT/godot-vita_export_templates.tpz" "${FILES[@]}"
python3 -m zipfile -t "$OUT/godot-vita_export_templates.tpz"
echo "TPZ créé: $OUT/godot-vita_export_templates.tpz"
