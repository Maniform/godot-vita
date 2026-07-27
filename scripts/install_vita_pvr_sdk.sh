#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
VERSION=${PVR_PSP2_VERSION:-3.9}
if [[ -n ${VITASDK:-} ]]; then
  DEFAULT_VITASDK=$VITASDK
elif [[ $(uname -s) == Darwin ]]; then
  DEFAULT_VITASDK=$ROOT/.toolchains/vitasdk
else
  DEFAULT_VITASDK=/usr/local/vitasdk
fi
PREFIX=${1:-"$DEFAULT_VITASDK/arm-vita-eabi"}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

curl -fL "https://github.com/GrapheneCt/PVR_PSP2/archive/refs/tags/v${VERSION}.tar.gz" -o "$WORK/pvr.tar.gz"
curl -fL "https://github.com/GrapheneCt/PVR_PSP2/releases/download/v${VERSION}/vitasdk_stubs.zip" -o "$WORK/stubs.zip"
tar -xf "$WORK/pvr.tar.gz" -C "$WORK"
python3 -m zipfile -e "$WORK/stubs.zip" "$WORK/stubs"

install -d "$PREFIX/include" "$PREFIX/lib"
cp -R "$WORK/PVR_PSP2-${VERSION}/include/." "$PREFIX/include/"
find "$WORK/stubs" -type f -name '*.a' -exec cp '{}' "$PREFIX/lib/" \;

for file in \
  "$PREFIX/include/gpu_es4/psp2_pvr_hint.h" \
  "$PREFIX/lib/liblibgpu_es4_ext_stub.a" \
  "$PREFIX/lib/liblibIMGEGL_stub.a" \
  "$PREFIX/lib/liblibGLESv2_stub.a"; do
  test -f "$file" || { echo "PVR_PSP2 file missing after installation: $file" >&2; exit 1; }
done

echo "PVR_PSP2 ${VERSION} installed in $PREFIX"
