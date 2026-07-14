# Build scripts

## Complete setup

`setup_godot_vita.sh` prepares Ubuntu 24.04, installs VitaSDK and PVR_PSP2,
configures MinGW, and builds the editors and export templates:

```bash
scripts/setup_godot_vita.sh
```

The script adds a managed block to `~/.bashrc` for `VITASDK` and `PATH`. It is
safe to run repeatedly, and SCons builds use the repository cache. If VitaSDK
is already installed, the bootstrap and `install-all.sh` steps are skipped and
only `vitasdk-update` is executed. Set `FORCE_VITASDK_INSTALL=yes` to force a
complete reinstall.

Main options:

- `--install-only`: install without building;
- `--build-only`: build without installing;
- `--with-cross-arch`: add Windows binaries for the opposite CPU architecture
  using LLVM-MinGW.

Cross-compiling Linux for the opposite CPU architecture is not enabled because
this branch's X11 platform code does not support it. The Linux binary therefore
always targets the host architecture.

## Specialized scripts

- `install_vita_pvr_sdk.sh` installs PVR_PSP2;
- `install_windows_cross_dependencies.sh` installs LLVM-MinGW for the host
  architecture;
- `build_editors.sh` builds the editors;
- `build_export_templates.sh` builds and bundles the export templates into a
  TPZ file.

VitaGL is disabled by default. To enable it for a specific build:

```bash
VITAGL=yes scripts/build_export_templates.sh vita
```
