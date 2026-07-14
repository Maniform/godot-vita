#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
VITASDK=${VITASDK:-/usr/local/vitasdk}
VDPM_DIR=${VDPM_DIR:-$ROOT/.toolchains/vdpm}
WITH_CROSS_ARCH=no
SKIP_INSTALL=no
SKIP_BUILD=no

usage() {
  cat <<'EOF'
Usage: scripts/setup_godot_vita.sh [options]

  --with-cross-arch  Install LLVM-MinGW for the opposite Windows architecture
  --install-only     Install and configure without building
  --build-only       Run the builds without installing
  -h, --help         Show this help

Useful variables: VITASDK, JOBS, LLVM_MINGW_VERSION, PVR_PSP2_VERSION.
EOF
}

while (($#)); do
  case "$1" in
    --with-cross-arch) WITH_CROSS_ARCH=yes ;;
    --install-only) SKIP_BUILD=yes ;;
    --build-only) SKIP_INSTALL=yes ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

if [[ $(uname -s) != Linux ]]; then
  echo "This script requires Ubuntu 24.04 or Ubuntu 24.04 under WSL." >&2
  exit 1
fi

if [[ -r /etc/os-release ]]; then
  . /etc/os-release
  if [[ ${ID:-} != ubuntu || ${VERSION_ID:-} != 24.04 ]]; then
    if [[ ${ALLOW_UNSUPPORTED_UBUNTU:-no} != yes ]]; then
      echo "Ubuntu 24.04 is required; detected system: ${PRETTY_NAME:-unknown}." >&2
      echo "Set ALLOW_UNSUPPORTED_UBUNTU=yes to continue at your own risk." >&2
      exit 1
    fi
    echo "Warning: running on ${PRETTY_NAME:-an unrecognized system}." >&2
  fi
fi

case "$(uname -m)" in
  x86_64) HOST_ARCH=x86_64 ;;
  aarch64|arm64) HOST_ARCH=aarch64 ;;
  *) echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

if [[ $EUID -eq 0 ]]; then
  SUDO=()
elif command -v sudo >/dev/null 2>&1; then
  SUDO=(sudo)
else
  echo "sudo is required to install VitaSDK and the Ubuntu packages." >&2
  exit 1
fi

configure_shell() {
  local bashrc=${BASHRC:-$HOME/.bashrc}
  local begin="# >>> godot-vita environment >>>"
  local end="# <<< godot-vita environment <<<"
  touch "$bashrc"
  if ! grep -Fq "$begin" "$bashrc"; then
    {
      printf '\n%s\n' "$begin"
      printf 'export VITASDK=%q\n' "$VITASDK"
      printf 'export PATH="$VITASDK/bin:$PATH"\n'
      printf '%s\n' "$end"
    } >> "$bashrc"
  fi
  export VITASDK
  export PATH="$VITASDK/bin:$PATH"
}

install_host_dependencies() {
  "${SUDO[@]}" apt-get update
  "${SUDO[@]}" apt-get install -y \
    build-essential git cmake python-is-python3 python3 python3-pip scons \
    pkg-config libx11-dev libxcursor-dev libxinerama-dev libxrandr-dev \
    libxi-dev libgl-dev libxext-dev libxrender-dev libasound2-dev \
    libpulse-dev libspeechd-dev libudev-dev mingw-w64 \
    curl xz-utils file zip unzip ca-certificates
}

install_vitasdk() {
  local update="$VITASDK/bin/vitasdk-update"
  local compiler="$VITASDK/bin/arm-vita-eabi-gcc"

  if [[ -x $update && -x $compiler && ${FORCE_VITASDK_INSTALL:-no} != yes ]]; then
    echo "VitaSDK is already installed in $VITASDK; skipping bootstrap."
    "$update"
    return
  fi

  mkdir -p "$(dirname "$VDPM_DIR")"
  if [[ -d "$VDPM_DIR/.git" ]]; then
    git -C "$VDPM_DIR" pull --ff-only
  else
    git clone https://github.com/vitasdk/vdpm "$VDPM_DIR"
  fi
  (cd "$VDPM_DIR" && ./bootstrap-vitasdk.sh)
  (cd "$VDPM_DIR" && ./install-all.sh)
  "$update"
}

select_mingw_posix() {
  local tool candidate
  for tool in gcc g++; do
    candidate="/usr/bin/x86_64-w64-mingw32-${tool}-posix"
    if [[ -x $candidate ]]; then
      "${SUDO[@]}" update-alternatives --set "x86_64-w64-mingw32-$tool" "$candidate"
    else
      echo "Warning: MinGW POSIX alternative not found: $candidate" >&2
    fi
  done
}

if [[ $SKIP_INSTALL == no ]]; then
  install_host_dependencies
  configure_shell
  install_vitasdk
  select_mingw_posix
  "${SUDO[@]}" env VITASDK="$VITASDK" PVR_PSP2_VERSION="${PVR_PSP2_VERSION:-3.9}" \
    "$ROOT/scripts/install_vita_pvr_sdk.sh" "$VITASDK/arm-vita-eabi"

  if [[ $HOST_ARCH == aarch64 || $WITH_CROSS_ARCH == yes ]]; then
    SKIP_APT=yes "$ROOT/scripts/install_windows_cross_dependencies.sh"
  fi
  if [[ $WITH_CROSS_ARCH == yes ]]; then
    echo "Note: this branch's platform/x11 code does not support cross-compiling Linux for the opposite architecture."
  fi
fi

if [[ $SKIP_BUILD == no ]]; then
  configure_shell
  cd "$ROOT"
  scripts/build_editors.sh linux
  if [[ $HOST_ARCH == x86_64 ]]; then
    scripts/build_editors.sh windows-x64
    if [[ $WITH_CROSS_ARCH == yes ]]; then
      scripts/build_editors.sh windows-arm64
    fi
  else
    scripts/build_editors.sh windows-arm64
    if [[ $WITH_CROSS_ARCH == yes ]]; then
      scripts/build_editors.sh windows-x64
    fi
  fi

  scripts/build_export_templates.sh linux
  if [[ $HOST_ARCH == x86_64 ]]; then
    scripts/build_export_templates.sh windows-x64
    if [[ $WITH_CROSS_ARCH == yes ]]; then
      scripts/build_export_templates.sh windows-arm64
    fi
  else
    scripts/build_export_templates.sh windows-arm64
    if [[ $WITH_CROSS_ARCH == yes ]]; then
      scripts/build_export_templates.sh windows-x64
    fi
  fi
  scripts/build_export_templates.sh vita
fi

echo
echo "godot-vita setup completed."
echo "Editors: $ROOT/bin"
echo "Templates: $ROOT/bin/export-templates/godot-vita_export_templates.tpz"
