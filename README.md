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

## Installation

The source repository used by these instructions is
[Maniform/godot-vita.git](https://github.com/Maniform/godot-vita.git).

### Ubuntu 24.04

#### 1. Why Ubuntu 24.04?

Ubuntu 24.04 is the supported and tested build environment, on both x86_64 and
ARM64. This Godot 3 branch still builds its Linux editor through the legacy X11
platform, so the X11 development libraries are required even if the editor
will not be launched on the build machine. The setup script installs the exact
X11 packages and compiler toolchains tested on Ubuntu 24.04; other releases may
provide incompatible package or compiler versions.

#### 2. Clone the repository

```bash
git clone https://github.com/Maniform/godot-vita.git
cd godot-vita
```

#### 3. Run the complete setup script

```bash
scripts/setup_godot_vita.sh
```

The script installs the Ubuntu dependencies, configures VitaSDK, installs
PVR_PSP2 and the Windows cross-compilation tools, then builds the native Linux
editor, the applicable Windows editor, and the export templates for those
platforms and Vita. It may ask for the `sudo` password while installing system
packages and VitaSDK.

The Linux editor dependencies include ALSA and PulseAudio audio support, Speech
Dispatcher for text-to-speech, and libudev for controller hotplugging.

The generated editors are placed in `bin/`. The bundle that can be imported
from the Godot export-template manager is written to:

```text
bin/export-templates/godot-vita_export_templates.tpz
```

Desktop template executables are intermediate packaging files. The build
script moves them to `bin/export-templates/staging/`, so `bin/` only retains
the standalone editor executables.

### Windows

The toolchain is designed for Linux. On Windows, run it inside Ubuntu 24.04
with WSL2.

#### 1. Install WSL2

Open PowerShell as Administrator and enable WSL without installing its default
distribution:

```powershell
wsl --install --no-distribution
```

Restart Windows if requested.

#### 2. Install Ubuntu 24.04 in WSL

Open PowerShell again and run:

```powershell
wsl --install -d Ubuntu-24.04
```

Launch Ubuntu 24.04, create the Linux user when prompted, then confirm from
PowerShell that the distribution uses WSL2:

```powershell
wsl --list --verbose
```

If the `VERSION` column does not show `2`, convert the distribution with:

```powershell
wsl --set-version Ubuntu-24.04 2
```

#### 3. Continue with the Ubuntu installation

Open the Ubuntu 24.04 terminal and follow the
[Ubuntu instructions](#ubuntu-2404), starting at step 2. Before cloning, move
to the Linux home directory so the repository is stored in WSL's Linux
filesystem rather than on a mounted Windows drive:

```bash
cd ~
```

Then run the clone and setup commands from the Ubuntu section unchanged.

After the build, Windows can access the generated files through WSL's network
share. For example, enter this location in File Explorer and open the Linux
user's `godot-vita/bin` directory:

```text
\\wsl$\Ubuntu-24.04\home
```

## Build and setup scripts

The complete setup command above is sufficient for the standard installation.
The scripts can also be run individually to install only selected toolchains,
build one editor or export target, change the number of parallel jobs, or choose
VitaGL instead of PVR_PSP2.

See [`scripts/README.md`](scripts/README.md) for every script's purpose,
accepted arguments, environment variables, defaults, and generated files.

## Upstream Godot project

Godot Engine is free and open source software distributed under the MIT
license. General engine documentation is available from the
[official Godot documentation](https://docs.godotengine.org), and the upstream
source repository is hosted at [godotengine/godot](https://github.com/godotengine/godot).

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`LICENSE.txt`](LICENSE.txt) for the
upstream contribution and licensing information included with this source
tree.
