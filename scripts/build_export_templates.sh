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
OUT=${TEMPLATE_OUTPUT_DIR:-$ROOT/bin/export-templates}
STAGE="$OUT/staging"
ARM64_PREFIX=${MINGW_ARM64_PREFIX:-$ROOT/.toolchains/llvm-mingw/bin/aarch64-w64-mingw32-}
X64_LLVM_PREFIX=${MINGW_X64_LLVM_PREFIX:-$ROOT/.toolchains/llvm-mingw/bin/x86_64-w64-mingw32-}
VITAGL=${VITAGL:-no}
if [[ -z ${VITASDK:-} ]]; then
  if [[ $(uname -s) == Darwin ]]; then
    VITASDK=$ROOT/.toolchains/vitasdk
  else
    VITASDK=/usr/local/vitasdk
  fi
fi
export VITASDK
VITA_PVR_SDK=${VITA_PVR_SDK:-$VITASDK/arm-vita-eabi}
mkdir -p "$STAGE"

check_vitasdk() {
  local vitasdk=$VITASDK
  local compiler="$vitasdk/bin/arm-vita-eabi-g++"

  test -x "$compiler" || {
    echo "VitaSDK compiler not found: $compiler" >&2
    echo "Run scripts/setup_godot_vita.sh --install-only first." >&2
    exit 1
  }

  if ! "$compiler" -x c++ -c /dev/null -o /dev/null >/dev/null 2>&1; then
    echo "VitaSDK compiler cannot run on the $(uname -m) host: $compiler" >&2
    if [[ $(uname -s) == Linux && ($(uname -m) == aarch64 || $(uname -m) == arm64) ]]; then
      echo "VitaSDK uses x86-64 tools; install qemu-user-binfmt, libc6:amd64 and libzstd1:amd64." >&2
      echo "Run scripts/setup_godot_vita.sh --install-only to install them." >&2
    fi
    exit 1
  fi
}

build_windows_x64() {
  local compiler_args=()
  local binary_arch=64
  if [[ $(uname -m) == aarch64 || $(uname -m) == arm64 ]]; then
    test -x "${X64_LLVM_PREFIX}clang++" || { echo "LLVM-MinGW x64 not found: ${X64_LLVM_PREFIX}clang++" >&2; exit 1; }
    compiler_args=(arch=x86_64 use_llvm=yes mingw_prefix_64="$X64_LLVM_PREFIX")
    binary_arch=x86_64
  fi
  scons platform=windows target=release tools=no bits=64 use_mingw=yes "${compiler_args[@]}" debug_symbols=no lto=none -j"$JOBS"
  mv -f "bin/godot.windows.opt.$binary_arch.exe" "$STAGE/windows_64_release.exe"
  scons platform=windows target=release_debug tools=no bits=64 use_mingw=yes "${compiler_args[@]}" debug_symbols=no lto=none -j"$JOBS"
  mv -f "bin/godot.windows.opt.debug.$binary_arch.exe" "$STAGE/windows_64_debug.exe"
}

build_windows_arm64() {
  test -x "${ARM64_PREFIX}clang++" || { echo "LLVM-MinGW ARM64 not found: ${ARM64_PREFIX}clang++" >&2; exit 1; }
  MINGW_ARM64_PREFIX="$ARM64_PREFIX" scons platform=windows target=release tools=no arch=arm64 bits=64 use_mingw=yes use_llvm=yes mingw_prefix_arm64="$ARM64_PREFIX" target_win_version=0x0A00 debug_symbols=no lto=none -j"$JOBS"
  mv -f bin/godot.windows.opt.arm64.exe "$STAGE/windows_arm64_release.exe"
  MINGW_ARM64_PREFIX="$ARM64_PREFIX" scons platform=windows target=release_debug tools=no arch=arm64 bits=64 use_mingw=yes use_llvm=yes mingw_prefix_arm64="$ARM64_PREFIX" target_win_version=0x0A00 debug_symbols=no lto=none -j"$JOBS"
  mv -f bin/godot.windows.opt.debug.arm64.exe "$STAGE/windows_arm64_debug.exe"
}

build_linux() {
  local binary_arch=64
  if [[ $(uname -m) == aarch64 || $(uname -m) == arm64 ]]; then
    binary_arch=arm64
  fi
  scons platform=x11 target=release tools=no bits=64 debug_symbols=no lto=none -j"$JOBS"
  mv -f "bin/godot.x11.opt.$binary_arch" "$STAGE/linux_x11_64_release"
  scons platform=x11 target=release_debug tools=no bits=64 debug_symbols=no lto=none -j"$JOBS"
  mv -f "bin/godot.x11.opt.debug.$binary_arch" "$STAGE/linux_x11_64_debug"
}

build_macos_binary() {
  local target=$1 arch=$2
  [[ $(uname -s) == Darwin ]] || {
    echo "macOS builds require a macOS host and the Xcode command-line tools." >&2
    exit 1
  }
  xcrun --sdk macosx --show-sdk-path >/dev/null
  scons platform=osx target="$target" tools=no arch="$arch" bits=64 debug_symbols=no lto=none -j"$JOBS"
}

package_macos() {
  local release_binary=$1 debug_binary=$2
  local package_root
  package_root=$(mktemp -d "${TMPDIR:-/tmp}/godot-vita-osx.XXXXXX")
  cp -R misc/dist/osx_template.app "$package_root/osx_template.app"
  mkdir -p "$package_root/osx_template.app/Contents/MacOS"
  cp "$release_binary" "$package_root/osx_template.app/Contents/MacOS/godot_osx_release.64"
  cp "$debug_binary" "$package_root/osx_template.app/Contents/MacOS/godot_osx_debug.64"
  chmod +x "$package_root/osx_template.app/Contents/MacOS/"*
  python3 scripts/package_templates.py zip-tree "$package_root" "$STAGE/osx.zip"
  rm -rf "$package_root"
}

build_macos_arch() {
  local arch=$1
  build_macos_binary release "$arch"
  build_macos_binary release_debug "$arch"
  package_macos "bin/godot.osx.opt.$arch" "bin/godot.osx.opt.debug.$arch"
}

build_macos_universal() {
  local universal_dir="$OUT/macos-universal"
  mkdir -p "$universal_dir"
  build_macos_binary release arm64
  build_macos_binary release_debug arm64
  build_macos_binary release x86_64
  build_macos_binary release_debug x86_64
  xcrun lipo -create bin/godot.osx.opt.arm64 bin/godot.osx.opt.x86_64 \
    -output "$universal_dir/godot_osx_release.64"
  xcrun lipo -create bin/godot.osx.opt.debug.arm64 bin/godot.osx.opt.debug.x86_64 \
    -output "$universal_dir/godot_osx_debug.64"
  package_macos "$universal_dir/godot_osx_release.64" "$universal_dir/godot_osx_debug.64"
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
  check_vitasdk
  build_vita_variant release vita_release.zip
  build_vita_variant release_debug vita_debug.zip
}

case "$TARGET" in
  all)
    if [[ $(uname -s) == Darwin ]]; then
      build_macos_universal
      build_vita
    else
      build_windows_x64
      build_windows_arm64
      build_linux
      build_vita
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
  vita) build_vita ;;
  *) echo "Usage: $0 [all|windows-x64|windows-arm64|linux|macos|macos-x64|macos-arm64|macos-universal|vita]" >&2; exit 2 ;;
esac

FILES=()
for file in "$STAGE"/*; do
  [[ -f $file ]] || continue
  FILES+=("$(basename "$file")=$file")
done
if ((${#FILES[@]} == 0)); then
  echo "No export template was produced in $STAGE." >&2
  exit 1
fi
python3 scripts/package_templates.py bundle "$ROOT" "$OUT/godot-vita_export_templates.tpz" "${FILES[@]}"
python3 -m zipfile -t "$OUT/godot-vita_export_templates.tpz"
echo "TPZ created: $OUT/godot-vita_export_templates.tpz"
