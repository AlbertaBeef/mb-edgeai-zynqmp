# Advanced: project structure and customization

This section is intended for users who want to modify the reference
designs — adding IP to the block design, changing constraints, or
adding packages or drivers to the PetaLinux project. It describes how
the repository is laid out, how the Make-driven build flow works, how
the PetaLinux side is organised, and what modifications have been
added on top of the stock AMD BSPs and the upstream Hailo Yocto layer.

The actual *build* instructions are in [build_instructions](build_instructions);
this section is about understanding the project well enough to modify
it.

## Repository layout

```
.
├── Makefile                   <- Top-level build entry point
├── README.md
├── config/                    <- Source-of-truth design metadata and auto-generation
│   ├── data.json
│   └── update.py
├── docs/                      <- This documentation (Sphinx + Read the Docs)
├── PetaLinux/
│   ├── Makefile               <- PetaLinux build orchestration
│   └── bsp/                   <- Per-board (and optional per-target) BSP fragments
│       └── pynqzu/, uzev/, zcu104/, zcu106/
├── submodules/
│   └── meta-hailo/            <- Hailo Yocto layer (git submodule, hailo8-scarthgap branch)
└── Vivado/
    ├── Makefile               <- Vivado build orchestration
    ├── scripts/
    │   ├── build.tcl          <- Project creation + block design assembly
    │   └── xsa.tcl            <- Synthesis, implementation, XSA export
    └── src/
        ├── bd/
        │   ├── bd_zynqmp.tcl  <- Block design for Zynq UltraScale+ targets
        │   └── mipi_locs.tcl  <- Per-target MIPI lane placement constants
        └── constraints/
            └── <target>.xdc   <- One XDC per target (pin assignments, timing)
```

This repository has no `Vitis/` directory — all supported targets are
PetaLinux-only (the Hailo accelerator runs from user-space on the
Linux side; there is no standalone equivalent).

Per-target build outputs are written to `Vivado/<target>/` and
`PetaLinux/<target>/`. None of these are committed.

The Vivado design is shared with [rpi-camera-fmc] in structure (same
`bd_name = rpi`, similar MIPI camera bring-up); this repository adds
the Hailo VPU on top.

[rpi-camera-fmc]: https://github.com/fpgadeveloper/rpi-camera-fmc

## Target naming

A `TARGET` is the canonical handle for a single design:

```
<board>[_<connector>]
```

Examples: `uzev`, `pynqzu`, `zcu104`, `zcu106`, `zcu106_hpc0`. The
first underscore-delimited token is taken as the *target board* and is
what `PetaLinux/Makefile` uses to select the BSP under
`PetaLinux/bsp/<board>/`.

The complete list of valid targets is in the `UPDATER START` block of
each Makefile and is generated from `config/data.json`.

## `config/data.json` and `config/update.py`

`config/data.json` is the canonical source of truth for the set of
supported designs and their per-target metadata. `config/update.py`
reads `data.json` and regenerates the auto-managed sections of the
Makefiles, the top-level `README.md`, and `.gitignore` — the sections
delimited by `UPDATER START` / `UPDATER END` comment markers.

When adding or modifying a target, edit `data.json` and re-run
`update.py`. Do not hand-edit content between the `UPDATER START` /
`UPDATER END` markers; it will be overwritten on the next regeneration.

## Make-driven build flow

There are three Makefiles in the repository:

| Makefile              | Scope                                                                                          |
|-----------------------|------------------------------------------------------------------------------------------------|
| `./Makefile`          | Top-level orchestration; assembles boot-image zips for one or all targets.                     |
| `./Vivado/Makefile`   | Creates the Vivado project, runs synthesis and implementation, exports the XSA.                |
| `./PetaLinux/Makefile`| Creates the PetaLinux project from the XSA, applies BSP overlays, integrates `meta-hailo`, builds, packages. |

A `make bootimage TARGET=<t>` invocation at the top level cascades:

```
make bootimage TARGET=t
  -> PetaLinux/Makefile petalinux TARGET=t
       -> ensures Vivado XSA exists
            Vivado/Makefile xsa TARGET=t
              -> vivado -mode batch -source scripts/build.tcl   (creates project)
              -> vivado -mode batch -source scripts/xsa.tcl     (synth, impl, XSA export)
       -> petalinux-create --template zynqMP --name t
       -> petalinux-config --get-hw-description <XSA>
       -> copy bsp/<board>/project-spec/* into the project
       -> copy bsp/<target>/project-spec/* into the project              (optional, if exists)
       -> copy submodules/meta-hailo into project-spec/meta-user/        (Hailo Yocto layer)
       -> petalinux-config --silentconfig
       -> petalinux-build
       -> petalinux-package boot ...
  -> zip the resulting boot files into bootimages/
```

Per-target lock files (`.<target>.lock`) prevent concurrent builds of
the same target from clobbering each other.

### `meta-hailo` integration

`PetaLinux/Makefile` brings the Hailo Yocto layer into each PetaLinux
project at configure time by copying `submodules/meta-hailo/` into the
target's `project-spec/meta-user/` directory. The submodule tracks
upstream Hailo's `hailo8-scarthgap` branch (pinned to a specific
SRCREV in `.gitmodules`), which is the branch Hailo maintains for
scarthgap-based builds and is already scarthgap-compat out of the box —
no local patch is applied on top.

Updating the Hailo layer means bumping the submodule pointer:

```
git -C submodules/meta-hailo fetch origin hailo8-scarthgap
git -C submodules/meta-hailo checkout <new-commit>
git add submodules/meta-hailo
```

Or, to follow the branch tip:

```
git submodule update --remote submodules/meta-hailo
```

## Vivado side

### Block design

The block-design scripts live under `Vivado/src/bd/`:

* `bd_zynqmp.tcl` — Zynq UltraScale+ targets.
* `mipi_locs.tcl` — Tcl dictionary mapping each target to its MIPI
  lane placement, sourced by `bd_zynqmp.tcl`.

`bd_zynqmp.tcl` contains per-board conditional blocks where a target
needs to deviate from the family defaults — typically for clock
routing, PS configuration, or PL pinout.

After sourcing the BD script, `scripts/build.tcl` runs
`validate_bd_design -force`, which triggers parameter propagation and
fills in connection-automation rules. As a result the final
implemented design may contain nets that aren't visible in the BD TCL
source — to see the actual netlist as built, inspect the saved `.bd`
file under `Vivado/<target>/<target>.srcs/sources_1/bd/<bd_name>/` or
use `write_bd_tcl` to export a complete script from an open project.

### Constraints

`Vivado/src/constraints/<target>.xdc` contains pin assignments and any
target-specific timing constraints. Constraints common to all targets
of a given family are not factored out — each target's XDC is
self-contained.

### Build scripts

* `Vivado/scripts/build.tcl` creates the Vivado project, adds the
  target's XDC, sources `bd_zynqmp.tcl`, and validates the block
  design. Invoked via `make project TARGET=<t>`.
* `Vivado/scripts/xsa.tcl` opens the existing project, runs synthesis
  and implementation, exports the XSA, and writes the bitstream into
  the implementation run directory. Invoked via `make xsa TARGET=<t>`.

Both scripts check `XILINX_VIVADO` to confirm the installed Vivado
version matches the `version_required` constant at the top of the
file.

### Modifying the block design

Edit `Vivado/src/bd/bd_zynqmp.tcl` directly. If the change applies
only to some targets, wrap the additions in the appropriate per-board
conditional block.

Once the script is edited, delete any existing per-target Vivado
project directory (`rm -rf Vivado/<target>`) and re-run the Vivado
build through the Makefile:

```
make -C Vivado xsa TARGET=<target>
```

This re-creates the project, sources the modified BD script, runs
`validate_bd_design`, synthesises, implements, and re-exports the XSA.
Downstream PetaLinux / boot-image steps will pick up the new XSA on
the next `make` at the top level.

### Adding or modifying constraints

Edit `Vivado/src/constraints/<target>.xdc` directly. If a constraint
applies to all targets in a family, it still needs to be replicated to
each target's XDC.

## PetaLinux side

### BSP composition

The PetaLinux project for a given target is composed at build time
from up to two BSP fragments plus the Hailo Yocto layer:

1. A **board BSP** at `PetaLinux/bsp/<board>/` — always applied.
   Provides board-specific kernel and U-Boot configuration, the
   system device-tree fragment for the board, the `initcams` startup
   scripts (including a `hailodemo.sh` and a `yolov5m_yuv.hef`
   pre-compiled Hailo network), the recipes that add `xtl`, `xsimd`,
   and `xtensor` to the rootfs, and any board-specific patches.
2. An **optional per-target BSP overlay** at `PetaLinux/bsp/<target>/`
   — applied only if the directory exists (the `cp` is prefixed with
   `-` in the Makefile so a missing directory is not an error).
3. The **`meta-hailo` Yocto layer** copied verbatim from
   `submodules/meta-hailo/` (tracking the `hailo8-scarthgap` branch)
   into `project-spec/meta-user/meta-hailo/`. This adds the Hailo
   runtime (`libhailort`) and the TAPPAS application framework to the
   build. See [Hailo libraries and applications](#hailo-libraries-and-applications)
   below for how the layer is structured and how to add to it.

### Layout of a board BSP

```
PetaLinux/bsp/<board>/project-spec/
├── configs/
│   ├── config                <- petalinux-config: bootargs, rootfs, hostname
│   ├── rootfs_config         <- petalinux-config -c rootfs: included packages
│   ├── init-ifupdown/
│   │   └── interfaces        <- /etc/network/interfaces
│   └── busybox/
│       └── inetd.conf
└── meta-user/
    ├── conf/
    │   ├── user-rootfsconfig <- declares additional rootfs config options
    │   ├── petalinuxbsp.conf
    │   └── layer.conf
    ├── recipes-apps/
    │   └── initcams/         <- Startup scripts + Hailo demo network
    │       ├── initcams.bb
    │       └── files/
    │           ├── init_cams.sh
    │           ├── displaycams.sh
    │           ├── hailodemo.sh
    │           └── yolov5m_yuv.hef
    ├── recipes-support/      <- Hailo C++ deps added to rootfs
    │   ├── xtl/xtl_0.7.7.bbappend
    │   ├── xsimd/xsimd_11.2.0.bbappend
    │   └── xtensor/xtensor_0.24.7.bbappend
    ├── recipes-bsp/
    │   ├── device-tree/
    │   │   ├── device-tree.bbappend
    │   │   └── files/
    │   │       └── system-user.dtsi    <- board-specific DT additions
    │   ├── u-boot/
    │   │   ├── u-boot-xlnx_%.bbappend
    │   │   └── files/
    │   │       ├── bsp.cfg
    │   │       ├── platform-top.h
    │   │       └── *.patch             <- U-Boot source patches
    │   └── embeddedsw/                 <- (zcu104 only)
    │       ├── fsbl-firmware_%.bbappend
    │       └── files/
    │           └── zcu104_vadj_fsbl.patch
    └── recipes-kernel/
        └── linux/
            ├── linux-xlnx_%.bbappend
            └── linux-xlnx/
                └── bsp.cfg             <- kernel Kconfig additions
```

### Adding a package to the root filesystem

1. Append the new option to `bsp/<board>/project-spec/configs/rootfs_config`.
2. If the package is not in the default `petalinux-config -c rootfs`
   menu, also append a declaration line to
   `bsp/<board>/project-spec/meta-user/conf/user-rootfsconfig`.
3. If the package is not provided by an existing meta-layer (including
   `meta-hailo`), add a recipe under
   `bsp/<board>/project-spec/meta-user/recipes-apps/<package>/<package>.bb`.

### Adding a kernel config option

Append the option to
`bsp/<board>/project-spec/meta-user/recipes-kernel/linux/linux-xlnx/bsp.cfg`.

### Adding a device-tree fragment

Edit
`bsp/<board>/project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi`.
If you add new files, ensure they are listed in `SRC_URI:append` in
`device-tree.bbappend`.

### Adding a kernel patch or out-of-tree driver

1. Drop the patch file into
   `bsp/<board>/project-spec/meta-user/recipes-kernel/linux/linux-xlnx/`.
2. Add `SRC_URI:append = " file://<your-patch>.patch"` to
   `recipes-kernel/linux/linux-xlnx_%.bbappend`.

### Modifying U-Boot

The same pattern as the kernel, under
`bsp/<board>/project-spec/meta-user/recipes-bsp/u-boot/`. `bsp.cfg`
adds U-Boot Kconfig options; `platform-top.h` overrides the U-Boot
platform header; patches are listed in `SRC_URI:append` in
`u-boot-xlnx_%.bbappend`.

### Modifying the Hailo layer

See [Hailo libraries and applications](#hailo-libraries-and-applications)
below — that section covers the layer's structure, how to add new
recipes, and how to override existing ones via per-BSP bbappends.

## Hailo libraries and applications

The Hailo runtime, drivers, and gstreamer plugins reach the rootfs
through the `meta-hailo` Yocto layer, brought in as a git submodule
that tracks Hailo's `hailo8-scarthgap` branch.

### Layer layout

`submodules/meta-hailo/` (copied into each project's
`meta-user/meta-hailo/` at configure time):

```
meta-hailo/
├── meta-hailo-libhailort/        <- core runtime + CLI + gstreamer plugin
│   ├── classes/
│   │   └── hailort-base.bbclass  <- shared cmake recipe glue, offline-build hooks
│   ├── conf/layer.conf
│   ├── recipes-core/packagegroups/
│   │   └── packagegroup-hailo-hailort.bb
│   ├── recipes-gstreamer/libgsthailo/
│   │   └── libgsthailo_4.23.0.bb       <- HailoNet gstreamer element
│   └── recipes-hailo/
│       ├── libhailort/libhailort_4.23.0.bb   <- libhailort.so + headers
│       ├── hailortcli/hailortcli_4.23.0.bb   <- hailortcli + benchmark commands
│       ├── pyhailort/pyhailort_4.23.0.bb     <- Python bindings
│       └── hailort-service/hailort-service_4.23.0.bb
├── meta-hailo-accelerator/       <- kernel-side: hailo PCIe / I²C driver
│   ├── recipes-kernel/hailo-accelerator/
│   │   └── hailo-accelerator.bb
│   └── recipes-core/packagegroups/
│       └── packagegroup-hailo-accelerator.bb
└── meta-hailo-tappas/            <- TAPPAS app framework + post-processing libs
    ├── recipes-gstreamer/
    │   ├── hailo-post-processes/   (5.1.0)
    │   ├── libgsthailotools/       (5.1.0)
    │   ├── tappas-tracers/         (5.1.0)
    │   └── xtl, xtensor, xsimd     (TAPPAS C++ deps; version-pinned)
    └── recipes-core/packagegroups/
        └── packagegroup-hailo-tappas.bb
```

The three sublayers must be registered with bitbake's layer
configuration. The board BSPs in this repository carry the
registration in `project-spec/configs/config`:

```
CONFIG_USER_LAYER_0="${PROOT}/project-spec/meta-user/meta-hailo/meta-hailo-libhailort"
CONFIG_USER_LAYER_1="${PROOT}/project-spec/meta-user/meta-hailo/meta-hailo-accelerator"
CONFIG_USER_LAYER_2="${PROOT}/project-spec/meta-user/meta-hailo/meta-hailo-tappas"
```

`${PROOT}` is expanded to the PetaLinux project root by
`meta-xilinx-core/gen-machine-conf/lib/update_buildconf.py:AddUserLayers()`.
Without these lines `petalinux-config --silentconfig` does not pick up
the layers nested under `meta-user/meta-hailo/` and recipes from them
fail to parse.

### What gets installed in the rootfs

The board BSPs select the Hailo content via two paths in
`project-spec/configs/rootfs_config`:

* `CONFIG_packagegroup-hailo-accelerator=y` → pulls in the kernel
  module and userspace plumbing for the Hailo-8 device.
* `CONFIG_packagegroup-hailo-tappas=y` (or the `-dev-pkg` variant) →
  pulls in `hailo-post-processes`, `libgsthailo`, `libgsthailotools`,
  and the TAPPAS C++ dependencies.

`hailortcli` is enabled separately (`CONFIG_hailortcli=y`). Add or
remove these lines in a BSP's `rootfs_config` to control what ships.

### Initialisation and demo

Each board BSP ships a `recipes-apps/initcams/` recipe carrying camera
and Hailo startup scripts:

* `init_cams.sh` — programs the on-FMC clock generator, sensors, and
  brings the MIPI pipeline up.
* `displaycams.sh` — sets up the HDMI output pipeline.
* `hailodemo.sh` — runs a sample inference pipeline using
  `gst-launch-1.0` + the HailoNet gstreamer element against a bundled
  pre-compiled network file.
* `yolov5m_yuv.hef` — the bundled YOLOv5m network compiled for the
  Hailo-8 (≈10 MB binary in the rootfs).

To swap the bundled `.hef` for a different model, drop the new file
into the same `recipes-apps/initcams/files/` directory and update the
`SRC_URI` in `initcams.bb`. To use it from a different demo script,
add the script to the same directory and reference it from
`do_install`.

### Adding a new Hailo recipe (e.g. another sublayer, library, or app)

Three patterns, depending on the change:

1. **bbappend an existing meta-hailo recipe** — preferred when you
   want to tweak `EXTRA_OECMAKE`, add a `DEPENDS`, or override a file
   shipped by the recipe. Put the bbappend under the BSP, not in the
   submodule, so it's tracked in this repository:

   ```
   PetaLinux/bsp/<board>/project-spec/meta-user/recipes-hailo/
     └── libhailort/
         └── libhailort_4.23.0.bbappend
   ```

   The version `4.23.0` must match the PV of the recipe in
   `meta-hailo-libhailort/recipes-hailo/libhailort/libhailort_4.23.0.bb`.
   The bbappend dir is searched automatically once it lives under
   `meta-user/` of the active project.

2. **A new BB recipe that depends on Hailo** — for an application,
   library, or systemd service that builds against libhailort. Drop
   it under the BSP:

   ```
   PetaLinux/bsp/<board>/project-spec/meta-user/recipes-apps/<name>/
     ├── <name>_<ver>.bb         (recipe; DEPENDS = "libhailort", etc.)
     └── files/                  (source / patches)
   ```

   Add `CONFIG_<name>=y` to the BSP's `rootfs_config`.

3. **Modify the meta-hailo submodule itself** — only do this when the
   change really belongs in the layer (a new sublayer, an upstream
   bugfix backport, etc.) and you've decided not to upstream it to
   Hailo. The standard git-submodule workflow applies:
   `cd submodules/meta-hailo`, branch off `hailo8-scarthgap`, commit,
   then bump the parent repo's submodule pointer. Be aware that the
   `PetaLinux/Makefile` copies the submodule contents fresh into each
   project at configure time, so local-only changes you forgot to
   commit *do* get picked up by the next build — but only until the
   parent submodule pointer drifts.

### Non-standard things this repo does on top of stock meta-hailo

* **`packagegroup-hailo-tappas` no longer pulls in `tappas-apps`.** The
  Hailo 5.1.0 TAPPAS restructure removed the legacy `tappas-apps`
  recipe; the BSPs' `rootfs_config` files have had the corresponding
  `CONFIG_tappas-apps=y` line removed to match.
* **`meta-hailo-vpu` is not registered.** Upstream's `hailo8-scarthgap`
  branch removed the VPU sublayer (it was Hailo-15-specific); there's
  no `CONFIG_USER_LAYER_3=...meta-hailo-vpu` line.
* **Host CA-bundle workaround required for builds.** PetaLinux
  2025.2's eSDK ships a `git-native` whose CA path is the unrelocated
  placeholder `/usr/local/oe-sdk-hardcoded-buildpath/...`.
  libhailort's CMake `FetchContent_Declare` clones at `do_configure`
  and fails on that path. The build host needs a one-time `sudo`
  symlink — see the top-level `README.md` section *Build issue and
  workaround*. `PetaLinux/Makefile` runs a `check_ca_workaround`
  target as a prerequisite of `petalinux` and fails fast with the fix
  instructions if the symlink isn't present.
* **`LICENSE_PATH` is rewritten from `=` to `+=` in the project-local
  copy of `meta-hailo-tappas/conf/layer.conf`.** Upstream's
  `hailo8-scarthgap` branch declares
  `LICENSE_PATH = "${LAYERDIR}/licenses/"` (hard assignment), which
  clobbers contributions from prior layers (notably `meta-qt5`, which
  ships `The-Qt-Company-GPL-Exception-1.0` in its own `licenses/`
  dir). Any recipe whose `LICENSE` field references a non-SPDX
  license carried by another layer — qtbase being the most prominent
  — then fails `do_create_spdx` with
  *"Cannot find any text for license …"*. The Makefile runs a `sed`
  on the project-local copy after `cp -R $(HAILO_RECIPES)`, so the
  submodule is left untouched (and `git submodule update --remote`
  won't undo the fix). The underlying bug should be reported
  upstream against `hailo-ai/meta-hailo`.

## Modifications layered on the stock BSPs

The board BSPs in this repository started as the corresponding stock
AMD reference BSPs and have been modified in the following ways. This
list is the answer to *"what would I lose if I overwrote the BSP with
the stock one?"* — it is what to re-apply if you ever do that.

### All BSPs

* **Hostname / product name** set in `configs/config` via
  `CONFIG_SUBSYSTEM_HOSTNAME` and `CONFIG_SUBSYSTEM_PRODUCT`.
* **SD-card root filesystem** configured in `configs/config`:
  `CONFIG_SUBSYSTEM_ROOTFS_EXT4`, `CONFIG_SUBSYSTEM_SDROOT_DEV`,
  `CONFIG_SUBSYSTEM_USER_CMDLINE` (with `cma=` raised for video frame
  buffers and Hailo network buffers).
* **Custom `system-user.dtsi`** with device-tree nodes for the
  RPi-camera I²C bus, camera sensors, clock generator, frame-buffer /
  video pipeline, and the Hailo VPU PCIe endpoint.
* **`recipes-apps/initcams/`** providing the camera + Hailo startup
  scripts and the bundled pre-compiled Hailo network
  (`yolov5m_yuv.hef`).
* **`recipes-support/{xtl,xsimd,xtensor}_*.bbappend`** adding the
  C++ header-only libraries that the Hailo TAPPAS pipeline depends
  on to the rootfs (and to the SDK sysroot, for cross-compiling
  user applications).
* **U-Boot patch `0001-ubifs-distroboot-support.patch`**.

### ZCU104 BSP

* **FSBL patch `zcu104_vadj_fsbl.patch`** in
  `recipes-bsp/embeddedsw/files/`, registered via
  `fsbl-firmware_%.bbappend`. The ZCU104 FSBL is patched to program the
  on-board IRPS5401 PMBus regulator to 1.8V before the FMC PHYs
  come out of reset.

### UltraZed-EV (uzev) BSP

* **`CONFIG_YOCTO_MACHINE_NAME="zynqmp-generic"`** in `configs/config`
  (the UZ-EV is not a stock Xilinx eval board).
* **SD-card device set to `/dev/mmcblk1p2`** rather than the ZynqMP
  default `mmcblk0p2`.
* **`PRIMARY_SD_PSU_SD_1_SELECT=y`** to route the boot SD interface
  through PSU SD1 instead of SD0.
* **Custom `system-user.dtsi`** with UZ-EV-specific peripheral
  configuration.
* **`meta-xilinx-tools/recipes-bsp/uboot-device-tree/`** overlay that
  overrides the U-Boot device tree.

### PYNQ-ZU BSP

* **WILC WiFi driver overlay** under `recipes-modules/wilc/` adding
  support for the WILC1000 module fitted on the PYNQ-ZU carrier.

## Where build outputs land

| Path                                | Contents                                                                       |
|-------------------------------------|--------------------------------------------------------------------------------|
| `Vivado/<target>/`                  | Vivado project. `<bd_name>_wrapper.xsa` is the export.                          |
| `Vivado/<target>/<target>.runs/impl_1/<bd_name>_wrapper.bit` | Bitstream.                                              |
| `Vivado/logs/`                      | Per-target Vivado build logs.                                                   |
| `PetaLinux/<target>/`               | PetaLinux project. All Yocto build state lives here, including the assembled `meta-hailo` layer under `project-spec/meta-user/meta-hailo/`. |
| `PetaLinux/<target>/images/linux/`  | `BOOT.BIN`, `image.ub`, `boot.scr`, `rootfs.tar.gz`, etc.                       |
| `PetaLinux/<target>/build/build.log`| PetaLinux build log.                                                            |
| `bootimages/`                       | Per-target zipped boot files.                                                   |

None of these directories are committed to the repository.
