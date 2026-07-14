# Godot Vita

> [!WARNING]
> **AI-generated changes disclaimer:** Almost all modifications made to this
> fork beyond the original godot-vita codebase were generated with artificial
> intelligence. They have been compiled and tested for the documented use
> cases, but they have not received the same level of manual review as an
> official Godot release. Review the changes and test your project carefully
> before distributing a build.

This repository contains a PlayStation Vita port of Godot Engine 3.x, together
with scripts for preparing the toolchains, building desktop editors, and
creating Vita and desktop export templates.

## Additions over the base repository

This fork adds or extends support for the following PlayStation Vita input
hardware:

- rear touch screen;
- accelerometer;
- gyroscope;
- magnetometer.

The Vita export templates use the PowerVR PVR_PSP2 renderer by default
(`vitagl=no`). VitaGL remains available as an opt-in build configuration.

## Build and setup scripts

All project-specific scripts are located in [`scripts`](scripts):

- `setup_godot_vita.sh` — master setup script. Installs the Ubuntu packages,
  configures the VitaSDK environment, installs or updates VitaSDK, installs
  PVR_PSP2, configures MinGW, and optionally builds all applicable editors and
  export templates.
- `build_editors.sh` — builds the Godot Vita editor for native Linux, Windows
  x86_64, or Windows ARM64.
- `build_export_templates.sh` — builds release and debug export templates for
  Linux, Windows x86_64, Windows ARM64, and PlayStation Vita, then creates an
  importable `.tpz` bundle.
- `install_vita_pvr_sdk.sh` — installs the PVR_PSP2 headers and VitaSDK stub
  libraries required by `vitagl=no` builds.
- `install_windows_cross_dependencies.sh` — architecture-neutral entry point
  for installing LLVM-MinGW for Windows cross-compilation.
- `install_windows_arm64_dependencies.sh` — implementation used by the
  architecture-neutral Windows toolchain installer; retained under its
  original name for compatibility.
- `package_templates.py` — packages individual export templates and the final
  Godot export-template bundle.

More details and individual commands are available in
[`scripts/README.md`](scripts/README.md).

## Installation on Ubuntu 24.04

The supported host is Ubuntu 24.04 on x86_64 or ARM64. Start from a clone of
this repository and run:

```bash
git clone https://github.com/SonicMastr/godot-vita.git
cd godot-vita
scripts/setup_godot_vita.sh
```

The script installs the required development packages, including the X11 and
MinGW dependencies. It adds the following managed environment block to
`~/.bashrc`:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

It then installs VitaSDK through VDPM, installs PVR_PSP2, selects the MinGW
POSIX threading compiler, builds the editors applicable to the host, and
creates the export-template bundle.

To also build Windows binaries for the CPU architecture opposite to the host:

```bash
scripts/setup_godot_vita.sh --with-cross-arch
```

Useful partial modes:

```bash
# Install and configure dependencies without building.
scripts/setup_godot_vita.sh --install-only

# Build using an existing installation.
scripts/setup_godot_vita.sh --build-only
```

VitaSDK installation is idempotent. When a valid installation already exists,
the script skips the bootstrap and runs `vitasdk-update`. A complete reinstall
can be requested with:

```bash
FORCE_VITASDK_INSTALL=yes scripts/setup_godot_vita.sh
```

Generated files are placed under `bin/`. The importable export-template bundle
is written to:

```text
bin/export-templates/godot-vita_export_templates.tpz
```

Cross-compiling Linux for the CPU architecture opposite to the host is not
currently supported by this branch's X11 platform code. Linux builds therefore
target the host architecture. Windows cross-compilation uses MinGW-w64 for
x86_64 and LLVM-MinGW for ARM64 or cross-architecture builds.

## Installation on Windows with Ubuntu 24.04 under WSL2

Install WSL2 and Ubuntu 24.04 from an elevated PowerShell terminal:

```powershell
wsl --install -d Ubuntu-24.04
```

Restart Windows if requested, launch Ubuntu, and complete the initial Linux
user setup. Confirm that WSL2 is being used:

```powershell
wsl --list --verbose
```

Inside the Ubuntu terminal, clone the repository into the Linux filesystem for
better compilation performance, rather than under `/mnt/c`:

```bash
cd ~
git clone https://github.com/SonicMastr/godot-vita.git
cd godot-vita
scripts/setup_godot_vita.sh --with-cross-arch
```

After the build, Windows executables and the export-template TPZ are available
inside the repository's `bin` directory. They can be copied to Windows with
Explorer through:

```text
\\wsl$\Ubuntu-24.04\home\<linux-user>\godot-vita\bin
```

The Windows x86_64 editor runs normally on x86_64 Windows and through Windows
emulation on ARM64 Windows. The Windows ARM64 editor runs natively on ARM64
Windows.

## Upstream Godot project

Godot Engine is free and open source software distributed under the MIT
license. General engine documentation is available from the
[official Godot documentation](https://docs.godotengine.org), and the upstream
source repository is hosted at [godotengine/godot](https://github.com/godotengine/godot).

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`LICENSE.txt`](LICENSE.txt) for the
upstream contribution and licensing information included with this source
tree.
