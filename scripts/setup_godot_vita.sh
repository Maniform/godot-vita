#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
HOST_OS=$(uname -s)
if [[ $HOST_OS == Darwin ]]; then
  if [[ -z ${VITASDK:-} || ($VITASDK == /usr/local/vitasdk && ! -w /usr/local) ]]; then
    VITASDK=$ROOT/.toolchains/vitasdk
  fi
else
  VITASDK=${VITASDK:-/usr/local/vitasdk}
fi
VDPM_DIR=${VDPM_DIR:-$ROOT/.toolchains/vdpm}
WITH_CROSS_ARCH=no
SKIP_INSTALL=no
SKIP_BUILD=no

usage() {
  cat <<'EOF'
Usage: scripts/setup_godot_vita.sh [options]

  --with-cross-arch  Also build the opposite desktop architecture
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

if [[ $HOST_OS != Linux && $HOST_OS != Darwin ]]; then
  echo "This script requires Ubuntu 24.04, WSL, or macOS." >&2
  exit 1
fi

if [[ $HOST_OS == Linux && -r /etc/os-release ]]; then
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

if [[ $HOST_OS == Darwin ]]; then
  SUDO=()
elif [[ $EUID -eq 0 ]]; then
  SUDO=()
elif command -v sudo >/dev/null 2>&1; then
  SUDO=(sudo)
else
  echo "sudo is required to install VitaSDK and the Ubuntu packages." >&2
  exit 1
fi

configure_shell() {
  local default_rc=$HOME/.bashrc
  if [[ $HOST_OS == Darwin ]]; then
    default_rc=$HOME/.zshrc
  fi
  local shell_rc=${SHELL_RC:-${BASHRC:-$default_rc}}
  local begin="# >>> godot-vita environment >>>"
  local end="# <<< godot-vita environment <<<"
  local temp_rc
  touch "$shell_rc"
  if grep -Fq "$begin" "$shell_rc" || grep -Fq "$end" "$shell_rc"; then
    if ! grep -Fq "$begin" "$shell_rc" || ! grep -Fq "$end" "$shell_rc"; then
      echo "Incomplete godot-vita environment block in $shell_rc; fix or remove it and rerun." >&2
      exit 1
    fi
    temp_rc=$(mktemp "${TMPDIR:-/tmp}/godot-vita-shell.XXXXXX")
    awk -v begin="$begin" -v end="$end" '
      $0 == begin { skip = 1; next }
      $0 == end { skip = 0; next }
      !skip { print }
    ' "$shell_rc" > "$temp_rc"
    mv "$temp_rc" "$shell_rc"
  fi
  {
    printf '\n%s\n' "$begin"
    printf 'export VITASDK=%q\n' "$VITASDK"
    printf 'export PATH="$VITASDK/bin:$PATH"\n'
    printf '%s\n' "$end"
  } >> "$shell_rc"
  configure_environment
}

configure_environment() {
  export VITASDK
  export PATH="$VITASDK/bin:$PATH"
}

configure_foreign_apt_sources() {
  local ubuntu_sources=/etc/apt/sources.list.d/ubuntu.sources
  local native_arch foreign_arch foreign_sources
  local codename=${VERSION_CODENAME:-noble}

  if [[ $HOST_ARCH == aarch64 ]]; then
    native_arch=arm64
    foreign_arch=amd64
  else
    native_arch=amd64
    foreign_arch=arm64
  fi
  foreign_sources="/etc/apt/sources.list.d/ubuntu-$foreign_arch.sources"

  if [[ -f $ubuntu_sources ]] && ! grep -q "^Architectures:" "$ubuntu_sources"; then
    "${SUDO[@]}" sed -i "/^Types:/a Architectures: $native_arch" "$ubuntu_sources"
  fi

  if [[ $foreign_arch == amd64 ]]; then
    "${SUDO[@]}" tee "$foreign_sources" >/dev/null <<EOF
Types: deb
URIs: http://archive.ubuntu.com/ubuntu/
Suites: $codename $codename-updates $codename-backports
Components: main universe restricted multiverse
Architectures: amd64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg

Types: deb
URIs: http://security.ubuntu.com/ubuntu/
Suites: $codename-security
Components: main universe restricted multiverse
Architectures: amd64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
  else
    "${SUDO[@]}" tee "$foreign_sources" >/dev/null <<EOF
Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports/
Suites: $codename $codename-updates $codename-backports $codename-security
Components: main universe restricted multiverse
Architectures: arm64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
  fi
}

install_host_dependencies() {
  if [[ $HOST_OS == Darwin ]]; then
    command -v brew >/dev/null 2>&1 || {
      echo "Homebrew is required. Install it from https://brew.sh and rerun this script." >&2
      exit 1
    }
    xcode-select -p >/dev/null 2>&1 || {
      echo "The Xcode command-line tools are required. Run: xcode-select --install" >&2
      exit 1
    }
    brew install cmake gnu-sed pkg-config python scons wget yasm
    return
  fi

  if [[ $HOST_ARCH == aarch64 || $WITH_CROSS_ARCH == yes ]]; then
    if [[ $HOST_ARCH == aarch64 ]]; then
      "${SUDO[@]}" dpkg --add-architecture amd64
    else
      "${SUDO[@]}" dpkg --add-architecture arm64
    fi
    configure_foreign_apt_sources
  fi

  "${SUDO[@]}" apt-get update
  "${SUDO[@]}" apt-get install -y \
    build-essential git cmake python-is-python3 python3 python3-pip scons \
    pkg-config libx11-dev libxcursor-dev libxinerama-dev libxrandr-dev \
    libxi-dev libgl-dev libxext-dev libxrender-dev libasound2-dev \
    libpulse-dev libspeechd-dev libudev-dev mingw-w64 \
    curl xz-utils file zip unzip ca-certificates

  if [[ $HOST_ARCH == aarch64 ]]; then
    "${SUDO[@]}" apt-get install -y qemu-user-binfmt libc6:amd64 libzstd1:amd64
  fi

  if [[ $WITH_CROSS_ARCH == yes ]]; then
    local cross_compiler foreign_arch
    if [[ $HOST_ARCH == aarch64 ]]; then
      cross_compiler=g++-x86-64-linux-gnu
      foreign_arch=amd64
    else
      cross_compiler=g++-aarch64-linux-gnu
      foreign_arch=arm64
    fi
    "${SUDO[@]}" apt-get install -y \
      "$cross_compiler" \
      "libx11-dev:$foreign_arch" "libxcursor-dev:$foreign_arch" \
      "libxinerama-dev:$foreign_arch" "libxrandr-dev:$foreign_arch" \
      "libxi-dev:$foreign_arch" "libgl-dev:$foreign_arch" \
      "libxext-dev:$foreign_arch" "libxrender-dev:$foreign_arch" \
      "libasound2-dev:$foreign_arch" "libpulse-dev:$foreign_arch" \
      "libspeechd-dev:$foreign_arch" "libudev-dev:$foreign_arch"
  fi
}

install_vitasdk() {
  local update="$VITASDK/bin/vitasdk-update"
  local compiler="$VITASDK/bin/arm-vita-eabi-gcc"

  if [[ -x $update && -x $compiler && ${FORCE_VITASDK_INSTALL:-no} != yes ]]; then
    if [[ ${SKIP_VITASDK_UPDATE:-no} == yes ]]; then
      echo "VitaSDK is already installed in $VITASDK; skipping update."
    elif [[ $HOST_OS == Darwin ]]; then
      local gnu_bin
      gnu_bin=$(brew --prefix gnu-sed)/libexec/gnubin
      env PATH="$gnu_bin:$PATH" "$update"
    else
      "$update"
    fi
    return
  fi

  mkdir -p "$(dirname "$VDPM_DIR")"
  if [[ -d "$VDPM_DIR/.git" ]]; then
    git -C "$VDPM_DIR" pull --ff-only
  else
    git clone https://github.com/vitasdk/vdpm "$VDPM_DIR"
  fi
  mkdir -p "$(dirname "$VITASDK")"
  (cd "$VDPM_DIR" && VITASDK="$VITASDK" ./bootstrap-vitasdk.sh)
  (cd "$VDPM_DIR" && ./install-all.sh)
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
  if [[ $HOST_OS == Linux ]]; then
    select_mingw_posix
    "${SUDO[@]}" env VITASDK="$VITASDK" PVR_PSP2_VERSION="${PVR_PSP2_VERSION:-3.9}" \
      "$ROOT/scripts/install_vita_pvr_sdk.sh" "$VITASDK/arm-vita-eabi"
    if [[ $HOST_ARCH == aarch64 || $WITH_CROSS_ARCH == yes ]]; then
      SKIP_APT=yes "$ROOT/scripts/install_windows_cross_dependencies.sh"
    fi
  else
    VITASDK="$VITASDK" PVR_PSP2_VERSION="${PVR_PSP2_VERSION:-3.9}" \
      "$ROOT/scripts/install_vita_pvr_sdk.sh" "$VITASDK/arm-vita-eabi"
  fi
fi

if [[ $SKIP_BUILD == no ]]; then
  configure_environment
  cd "$ROOT"
  if [[ $HOST_OS == Darwin ]]; then
    if [[ $WITH_CROSS_ARCH == yes ]]; then
      scripts/build_editors.sh macos-universal
      scripts/build_export_templates.sh macos-universal
    else
      scripts/build_editors.sh macos
      scripts/build_export_templates.sh macos
    fi
    scripts/build_export_templates.sh vita
  else
    if [[ $WITH_CROSS_ARCH == yes ]]; then
      scripts/build_editors.sh linux-universal
    else
      scripts/build_editors.sh linux
    fi
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

    if [[ $WITH_CROSS_ARCH == yes ]]; then
      scripts/build_export_templates.sh linux-universal
    else
      scripts/build_export_templates.sh linux
    fi
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
fi

echo
echo "godot-vita setup completed."
if [[ $HOST_OS == Darwin ]]; then
  echo "Editor: $ROOT/bin/Godot Vita.app"
else
  echo "Editors: $ROOT/bin"
fi
echo "Templates: $ROOT/bin/export-templates/godot-vita_export_templates.tpz"
