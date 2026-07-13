#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
VERSION=${LLVM_MINGW_VERSION:-20260407}
TOOLCHAINS_DIR=${TOOLCHAINS_DIR:-$ROOT/.toolchains}
INSTALL_DIR="$TOOLCHAINS_DIR/llvm-mingw-$VERSION"
LINK_DIR="$TOOLCHAINS_DIR/llvm-mingw"
ARCHIVE="llvm-mingw-${VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz"
URL="https://github.com/mstorsjo/llvm-mingw/releases/download/${VERSION}/${ARCHIVE}"

if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
  echo "Ce script nécessite un hôte Linux x86_64 (Ubuntu ou WSL)." >&2
  exit 1
fi

if [[ ${SKIP_APT:-no} != yes ]]; then
  if [[ $EUID -eq 0 ]]; then
    SUDO=()
  elif command -v sudo >/dev/null 2>&1; then
    SUDO=(sudo)
  else
    echo "sudo est requis pour installer les paquets Ubuntu (ou utilisez SKIP_APT=yes)." >&2
    exit 1
  fi

  "${SUDO[@]}" apt-get update
  "${SUDO[@]}" apt-get install -y \
    build-essential \
    git \
    python3 \
    python3-pip \
    scons \
    curl \
    xz-utils \
    file \
    zip
fi

mkdir -p "$TOOLCHAINS_DIR"

if [[ ! -x "$INSTALL_DIR/bin/aarch64-w64-mingw32-clang++" ]]; then
  WORK=$(mktemp -d)
  trap 'rm -rf "$WORK"' EXIT

  curl -fL --retry 3 "$URL" -o "$WORK/$ARCHIVE"
  tar -xf "$WORK/$ARCHIVE" -C "$WORK"
  mv "$WORK/${ARCHIVE%.tar.xz}" "$INSTALL_DIR"
fi

ln -sfn "$(basename "$INSTALL_DIR")" "$LINK_DIR"

PREFIX="$LINK_DIR/bin/aarch64-w64-mingw32-"
for tool in clang clang++ windres; do
  test -x "${PREFIX}${tool}" || { echo "Outil LLVM-MinGW manquant: ${PREFIX}${tool}" >&2; exit 1; }
done
test -x "$LINK_DIR/bin/llvm-ar" || { echo "Outil LLVM-MinGW manquant: $LINK_DIR/bin/llvm-ar" >&2; exit 1; }

"${PREFIX}clang++" --version
"${PREFIX}windres" --version

echo
echo "Dépendances Windows ARM64 installées."
echo "Toolchain: $LINK_DIR"
echo "Compilation: scripts/build_editors.sh windows-arm64"
