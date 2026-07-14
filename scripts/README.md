# Build and setup scripts

Run these commands from the repository root. The standard installation only
needs `scripts/setup_godot_vita.sh`; the other entry points are useful for
partial installations, rebuilding a single platform, or customizing the
toolchains.

## `setup_godot_vita.sh`

This is the main setup script. It verifies Ubuntu 24.04, installs the host and
X11 development packages, configures VitaSDK in the shell, installs or updates
VitaSDK and PVR_PSP2, configures MinGW, and builds the supported desktop
editors and export templates.

```text
scripts/setup_godot_vita.sh [options]
```

Options:

- `--with-cross-arch` installs LLVM-MinGW and also builds the Windows editor
  and export templates for the architecture opposite to the host: ARM64 on
  x86_64, or x86_64 on ARM64.
  It does not cross-compile the Linux editor because this branch's X11 platform
  does not support that configuration.
- `--install-only` installs and configures all dependencies, but does not run
  any builds. Use it to prepare a machine or repair its toolchains.
- `--build-only` skips package and SDK installation and builds with the tools
  already present. Use it for subsequent rebuilds.
- `-h` or `--help` prints the built-in usage summary.

Important environment variables:

- `VITASDK` changes the VitaSDK installation directory. The default is
  `/usr/local/vitasdk`.
- `VDPM_DIR` changes the local VDPM checkout. The default is
  `.toolchains/vdpm` in the repository.
- `JOBS` sets the number of parallel SCons jobs used by the build scripts. The
  default is the number of available CPU threads.
- `PVR_PSP2_VERSION` selects the PVR_PSP2 release. The default is `3.9`.
- `LLVM_MINGW_VERSION` selects the LLVM-MinGW release used for
  cross-compilation. The current default is `20260407`.
- `FORCE_VITASDK_INSTALL=yes` forces a full VitaSDK bootstrap instead of using
  `vitasdk-update` on an existing valid installation.
- `ALLOW_UNSUPPORTED_UBUNTU=yes` bypasses the Ubuntu 24.04 version check. This
  is unsupported and may fail because package or compiler versions differ.
- `BASHRC` changes the shell startup file updated by the script. The default is
  the current user's `~/.bashrc`.

The script manages the following environment settings in the shell startup
file and also exports them for the current run:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

It is safe to run the setup repeatedly. If VitaSDK is already valid, the script
skips its bootstrap and runs `vitasdk-update`. SCons also reuses the repository
cache between builds.

Examples:

```bash
# Complete installation and build for the host architecture.
scripts/setup_godot_vita.sh

# Prepare all dependencies without compiling.
scripts/setup_godot_vita.sh --install-only

# Rebuild without reinstalling dependencies.
scripts/setup_godot_vita.sh --build-only

# Also produce the Windows editor and templates for the opposite architecture.
scripts/setup_godot_vita.sh --with-cross-arch
```

## `build_editors.sh`

Builds `release_debug` desktop editors with `tools=yes`. Its single optional
positional argument selects the target:

```text
scripts/build_editors.sh [all|windows-x64|windows-arm64|linux]
```

- `all` is the default and builds Windows x86_64, Windows ARM64, and the native
  Linux editor. Both Windows toolchains must already be installed.
- `windows-x64` builds the 64-bit x86 Windows editor. It uses Ubuntu's
  MinGW-w64 on an x86_64 host and LLVM-MinGW when cross-compiling from ARM64.
- `windows-arm64` builds the native Windows ARM64 editor with LLVM-MinGW.
- `linux` builds the Linux X11 editor for the host CPU. `linux-x64` is accepted
  as a compatibility alias, but the script still builds for the host CPU.

Every command in this script sets `tools=yes`. Non-editor binaries with
`tools=no` are only produced by the separate export-template script.

Environment variables:

- `JOBS` controls parallel compilation; it defaults to `nproc`.
- `SCONS_CACHE` changes the SCons cache directory; it defaults to
  `.scons_cache` in the repository.
- `SCONS_CACHE_LIMIT` sets the cache limit in MiB; it defaults to `10240`.
- `MINGW_ARM64_PREFIX` overrides the ARM64 LLVM-MinGW executable prefix.
- `MINGW_X64_LLVM_PREFIX` overrides the x86_64 LLVM-MinGW executable prefix
  used on an ARM64 host.

## `build_export_templates.sh`

Builds both `release` and `release_debug` export templates for the selected
platform. After every run, it rebuilds and validates the importable TPZ bundle.

```text
scripts/build_export_templates.sh [all|windows-x64|windows-arm64|linux|vita]
```

- `all` is the default and builds every Windows, Linux, and Vita template. Both
  Windows toolchains must already be available.
- `windows-x64`, `windows-arm64`, and `linux` build only that platform's two
  template variants. `linux-x64` is also accepted as an alias for `linux`.
- `vita` builds `vita_release.zip` and `vita_debug.zip`. PVR_PSP2 is the default
  renderer; set `VITAGL=yes` to build with VitaGL instead.

The script accepts the same `JOBS`, `SCONS_CACHE`, and `SCONS_CACHE_LIMIT`
variables as `build_editors.sh`, plus:

- `TEMPLATE_OUTPUT_DIR` changes the output and staging directory. The default
  is `bin/export-templates`.
- `MINGW_ARM64_PREFIX` overrides the ARM64 LLVM-MinGW executable prefix.
- `MINGW_X64_LLVM_PREFIX` overrides the x86_64 LLVM-MinGW executable prefix
  used on an ARM64 host.
- `VITAGL` selects the Vita renderer: `no` uses PVR_PSP2 and `yes` uses VitaGL.
- `VITA_PVR_SDK` changes the PVR_PSP2 headers and stub-library prefix. The
  default is `$VITASDK/arm-vita-eabi`.

Example:

```bash
VITAGL=yes scripts/build_export_templates.sh vita
```

The final bundle is
`bin/export-templates/godot-vita_export_templates.tpz` by default.
Desktop template executables are moved from `bin/` to the staging directory
after each build; only editor executables remain directly in `bin/`.

## `install_vita_pvr_sdk.sh`

Downloads a PVR_PSP2 release, installs its headers and VitaSDK stub libraries,
then verifies the required files.

```text
scripts/install_vita_pvr_sdk.sh [installation-prefix]
```

- `installation-prefix` is an optional positional argument. It defaults to
  `$VITASDK/arm-vita-eabi`, or `/usr/local/vitasdk/arm-vita-eabi` when
  `VITASDK` is unset.
- `PVR_PSP2_VERSION` selects the release to download and defaults to `3.9`.

The destination must be writable. The main setup script invokes this installer
through `sudo`, so its default system destination works automatically.

## `install_windows_cross_dependencies.sh`

This is the architecture-neutral entry point for installing LLVM-MinGW. It
calls `install_windows_arm64_dependencies.sh`, whose historical name is kept
for compatibility. There are no command-line arguments.

The installer downloads the Linux LLVM-MinGW archive for the host CPU and
verifies both its x86_64 and ARM64 Windows compiler prefixes. It then exposes
the selected release through `.toolchains/llvm-mingw`.

Environment variables:

- `LLVM_MINGW_VERSION` selects the release and defaults to `20260407`.
- `TOOLCHAINS_DIR` changes the installation parent directory and defaults to
  `.toolchains` in the repository.
- `SKIP_APT=yes` skips installation of the Ubuntu packages. The main setup
  script uses this after it has already installed the shared dependencies.

Use `install_windows_cross_dependencies.sh` for new commands and automation;
invoke `install_windows_arm64_dependencies.sh` directly only for compatibility
with existing workflows.

## `package_templates.py`

This internal helper creates ZIP files and the final Godot template bundle. It
is normally called by `build_export_templates.sh` rather than manually.

```text
python3 scripts/package_templates.py zip-tree SOURCE OUTPUT
python3 scripts/package_templates.py vita APP_DIRECTORY EBOOT OUTPUT
python3 scripts/package_templates.py bundle ROOT OUTPUT NAME=SOURCE [NAME=SOURCE ...]
```

- `zip-tree` archives every file below `SOURCE` into `OUTPUT`.
- `vita` writes `EBOOT` as `eboot.bin`, adds the contents of `APP_DIRECTORY`,
  and creates the Vita template at `OUTPUT`.
- `bundle` reads the Godot version from `ROOT`, writes `version.txt`, and adds
  each `NAME=SOURCE` mapping to the TPZ file at `OUTPUT`.
