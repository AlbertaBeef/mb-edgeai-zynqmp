# Multi-M.2-accelerator Edge AI on the Zynq UltraScale+

> This project is derived from Opsero's
> [zynqmp-hailo-ai](https://github.com/fpgadeveloper/zynqmp-hailo-ai) reference design and
> extends it to support a range of **M.2 AI accelerators** — Hailo-8, Axelera Metis, DeepX M1
> and MemryX MX3 — following the integration described in
> [Edge AI on AMD PetaLinux 2025.2](https://mariobergeron.com/posts/edge-ai-yocto-p01-amd-petalinux-2025-2/).
> See [Modifications from the original design](#modifications-from-the-original-design) for what changed.

## Description

This project demonstrates the combined power of the Zynq UltraScale+ and the Hailo-8 AI accelerator
when used in multi-camera vision applications. The repo contains designs for several Zynq UltraScale+
development boards and connects to 4x Raspberry Pi cameras via the Opsero [RPi Camera FMC]. 
The Hailo-8 AI accelerator connects to the development board via the [FPGA Drive FMC Gen4]
or the [M.2 M-key Stack FMC] depending on the target design (see list of target designs below).

A detailed description of this design and how to use it was written up in this blog post:
[Multi-camera YOLOv5 on Zynq UltraScale+ with Hailo-8 AI Acceleration](https://www.fpgadeveloper.com/multi-camera-yolov5-on-zynq-ultrascale-with-hailo-8-ai-acceleration/)

![Multi-camera YOLOv5 on ZynqMP and Hailo-8](https://www.fpgadeveloper.com/multi-camera-yolov5-on-zynq-ultrascale-with-hailo-8-ai-acceleration/images/zynqmp-hailo-ai-front.jpg "Multi-camera YOLOv5 on ZynqMP and Hailo-8")

Important links:
* The [user guide](https://hailo.camerafmc.com) for these reference designs
* Datasheet of the [RPi Camera FMC]
* Datasheet of the [FPGA Drive FMC Gen4] (for the `zcu106` target design only)
* Datasheet of the [M.2 M-key Stack FMC]
* To [report an issue](https://github.com/fpgadeveloper/zynqmp-hailo-ai/issues)
* For technical support: [Contact Opsero](https://opsero.com/contact-us)

## Modifications from the original design

This repository is derived from Opsero's
[zynqmp-hailo-ai](https://github.com/fpgadeveloper/zynqmp-hailo-ai) reference design, which
targets the Hailo-8 only. The changes below extend it to support several M.2 AI accelerators
(Hailo-8, Axelera Metis, DeepX M1, MemryX MX3), based on the integration work described in
[this blog post](https://mariobergeron.com/posts/edge-ai-yocto-p01-amd-petalinux-2025-2/).
The camera capture pipelines, DisplayPort output and VCU of the original design are unchanged.

### Hardware (Vivado)

* **Second PCIe BAR per root port** — each XDMA (PCIe root port) now exposes a second BAR: a
  128 MB non-prefetchable 32-bit window. Across the two root ports these fill
  `0xB000_0000`–`0xBFFF_FFFF`. The original design's single prefetchable 64-bit BAR is not
  sufficient for the multi-BAR accelerators (Axelera, DeepX, MemryX), which need this extra
  32-bit window to enumerate.
* **M.2 Stack FMC `PERST#` control** — the [M.2 M-key Stack FMC] drives `PERST_A#`/`PERST_B#`
  of its two M.2 slots through a TCA9536 I2C I/O expander. Depending on where each board
  routes the FMC I2C bus, this is driven either by an AXI IIC added in the PL (`pl_i2c`) or by
  the PS I2C1 (`ps_i2c`) — see [M.2 Stack FMC PERST# control](#m2-stack-fmc-perst-control).
* **FPGA Drive FMC `PERST#` control (`zcu106` only)** — on the base `zcu106` target the M.2
  adapter is the [FPGA Drive FMC Gen4] on HPC1, whose reset/detect signals are wired to PL I/O
  rather than an I2C expander. A dedicated dual-channel AXI GPIO drives them: channel 1 (outputs,
  default `0b11`) holds `perst_a`/`perst_b` de-asserted at PL config plus a `disable_ssd2_pwr`
  control; channel 2 (inputs) reads `pedet_a`/`pedet_b`. `PERST#` is already released when the
  PL configures; the base `zcu106` FSBL additionally pulses it low→high after bitstream download
  via this GPIO (see below), so slow-training endpoints re-train before Linux probes PCIe.

These changes are implemented in the block-design generator `Vivado/src/bd/bd_zynqmp.tcl`: the
second BAR is added for all boards, the AXI IIC is added only on `pl_i2c` boards, and the FPGA
Drive FMC AXI GPIO is gated to the `zcu106` target. The FPGA Drive FMC pin/IOSTANDARD
constraints live in `Vivado/src/constraints/zcu106.xdc`.

### Software (PetaLinux)

The accelerator support is added as git submodules under `submodules/`. At build time each
is copied into the PetaLinux project's `project-spec/` (not `project-spec/meta-user/`) and
registered as a bitbake layer via `CONFIG_USER_LAYER_*` in `project-spec/configs/config`:

| Meta-layer | Accelerator | Repository | Branch |
|------------|-------------|------------|--------|
| `meta-hailo` | Hailo-8 | [hailo-ai/meta-hailo](https://github.com/hailo-ai/meta-hailo) | `hailo8-scarthgap` |
| `meta-axelera` | Axelera Metis | [axelera-ai-hub/meta-axelera](https://github.com/axelera-ai-hub/meta-axelera) | `yocto/scarthgap` |
| `meta-deepx-m1` | DeepX M1 | [DEEPX-AI/meta-deepx-m1](https://github.com/DEEPX-AI/meta-deepx-m1) | `scarthgap` |
| `memx-yocto` | MemryX MX3 | [AlbertaBeef/memx-yocto](https://github.com/AlbertaBeef/memx-yocto) | `mb-edgeai-amd-petalinux-2025-2` |

The `memx-yocto` layer is a fork that adds the scarthgap support the upstream MemryX layer lacks.

#### Device tree (`project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi`)

* **Non-prefetchable BAR ranges** — the `axi-pcie@…` `ranges` are overridden so the second
  32-bit BAR is exposed as non-prefetchable memory. Without this the multi-BAR accelerators
  fail with *"eDMA BAR I/O remapping failed"* at probe.
* **Ethernet PHY reset (`zcu104`, `zcu106`)** — the on-board DP83867 PHY on `gem3` needs an
  explicit reset to come up reliably (otherwise it falls back to a random MAC / no link). The
  device tree now adds a `phy-handle`/`phy-mode` on `gem3` and an `ethernet-phy` node whose
  `reset-gpios` point at the board's TCA6416 I/O expander (`tca6416_u97`). On `zcu104` that
  expander sits behind the PS-I2C TCA9548 switch; on `zcu106` it is directly on `i2c0`.
* **M.2 Stack FMC TCA9536 (`zcu104`, `zcu106_hpc0`)** — the M.2 Stack FMC's TCA9536 expander is
  now described, reached through the correct TCA9548 downstream channel (`zcu104`: channel 5;
  `zcu106_hpc0`: HPC0 channel 0). A `gpio-hog` drives `PERST_A#`/`PERST_B#` high so the M.2
  slots are out of reset before Linux probes PCIe.

#### FSBL (`project-spec/meta-user/recipes-bsp/embeddedsw/`)

* **Boot-time `PERST#`** — an `xfsbl_hooks.c` hook pulses `PERST_A#`/`PERST_B#` early in boot,
  so slow-to-train endpoints are up before the PCIe root complex is probed. The mechanism is
  target-specific:
    * `uzev` — M.2 Stack FMC TCA9536 over the **PL AXI IIC** (`pl_i2c`).
    * `zcu104`, `zcu106_hpc0` — M.2 Stack FMC TCA9536 reached by walking the **PS-I2C TCA9548**
      switch (`ps_i2c`).
    * `zcu106` (base) — the FPGA Drive FMC's `PERST#` is on PL I/O, so the hook drives the
      **`fpga_drive_gpio` AXI GPIO** directly (memory-mapped, no I2C). This is a separate
      `xfsbl_hooks.c` in `bsp/zcu106`; the `bsp/zcu106_hpc0` overlay swaps in the TCA9536 version
      for that target.

  The hook is installed via `do_configure:prepend` with `install` (not `cp`, which would corrupt
  the hardlinked shared embeddedsw source tree).

#### Login prompt / hostname

* The Linux hostname and product string are set to `mb-edgeai-<board>-2025-2` (in
  `project-spec/configs/config`), replacing the original `<board>-hailo-2025-2`.

### Supported boards

* PYNQ-ZU support has been dropped. Supported targets: `zcu104`, `zcu106`, `zcu106_hpc0`, `uzev`.

### Validation status

The Vivado, meta-layer, device tree and FSBL changes above are all in place and build. Hardware
validation so far:

| Target | Build | Hardware validation |
|--------|-------|---------------------|
| `zcu104`      | :white_check_mark: | :white_check_mark: DeepX M1 **and** MemryX MX3 enumerate and run (ps_i2c `PERST#`, ethernet PHY reset confirmed) |
| `uzev`        | :white_check_mark: | :hourglass: builds; `pl_i2c` `PERST#` path not yet confirmed on hardware |
| `zcu106`      | :white_check_mark: | :grey_question: **untested — no board on hand.** FPGA Drive FMC AXI GPIO + `zcu106.xdc` constraints pass synthesis but the pin mapping and PCIe link are unverified |
| `zcu106_hpc0` | :white_check_mark: | :grey_question: **untested — no board on hand.** PS-I2C mux path (I2C1 → PCA9548 `0x75` → ch0 → TCA9536) assumed from the stock DTS/schematic |

The `zcu106`/`zcu106_hpc0` changes are deliberately **fail-safe**: a wrong I2C path just NACKs,
and a wrong GPIO/PL pin only means `PERST#` isn't delivered — neither can damage hardware. They
should be re-checked on the first available ZCU106 (watch the `[PERST]` FSBL prints, ethernet
link, and PCIe enumeration).

## Requirements

This project is designed for version 2025.2 of the Xilinx tools (Vivado/Vitis/PetaLinux). 
If you are using an older version of the Xilinx tools, then refer to the 
[release tags](https://github.com/fpgadeveloper/zynqmp-hailo-ai/tags "releases")
to find the version of this repository that matches your version of the tools.

In order to test this design on hardware, you will need the following:

* Vivado 2025.2
* Vitis 2025.2
* PetaLinux Tools 2025.2
* 1x [Hailo-8 M.2 AI Acceleration Module]
* 4x [Raspberry Pi Camera Module 2](https://www.raspberrypi.com/products/camera-module-v2/)
* 1x [RPi Camera FMC]
* 1x [FPGA Drive FMC Gen4] or 1x [M.2 M-key Stack FMC]
* 1x DisplayPort monitor (1080p minimum resolution, 2K/2560x1440 ideal)
* Alternatively, 1x HDMI monitor and DP-to-HDMI adapter
* 1x of the supported target boards (see target designs table)

Below are images of some of the required parts.

| RPi Camera FMC | Hailo-8 M.2 AI Module |
|---------------------|---------------------|
| ![RPi Camera FMC](docs/source/images/rpi-camera-fmc-top-angle.png "RPi Camera FMC") | ![Hailo-8](docs/source/images/hailo-ai.jpg "Hailo-8") |

| FPGA Drive FMC Gen4 | M.2 M-key Stack FMC |
|---------------------|---------------------|
| ![FPGA Drive FMC Gen4](docs/source/images/fpga-drive-fmc-gen4.png "FPGA Drive FMC Gen4") | ![M.2 M-key Stack FMC](docs/source/images/m2-mkey-stack-fmc.png "M.2 M-key Stack FMC") |

## Target designs

Note that there are two target designs for the [ZCU106] board: `zcu106` and `zcu106_hpc0`, and the
differences are explained in the table below.
All target designs except `zcu106` require the [M.2 M-key Stack FMC] as the M.2 adapter for the Hailo-8, with the
[RPi Camera FMC] stacked on top of it.

<!-- updater start -->
### Zynq UltraScale+ designs

| Target board          | Target design   | FMC Slot(s) | Cameras | Active M.2 Slots | VCU   | Stack Design | Vivado<br> Edition | IP<br>License |
|-----------------------|-----------------|-------------|---------|------------------|-------|--------------|-------|-------|
| [ZCU104]              | `zcu104`        | LPC         | 4     | 1     | :white_check_mark: | :white_check_mark: | Standard :free: | -     |
| [ZCU106]              | `zcu106`        | HPC0+HPC1   | 4     | 1     | :white_check_mark: | :x:                | Standard :free: | -     |
| [ZCU106]              | `zcu106_hpc0`   | HPC0        | 4     | 2     | :white_check_mark: | :white_check_mark: | Standard :free: | -     |
| [UltraZed-EV Carrier] | `uzev`          | HPC         | 4     | 2     | :white_check_mark: | :white_check_mark: | Standard :free: | -     |

[ZCU104]: https://www.xilinx.com/zcu104
[ZCU106]: https://www.xilinx.com/zcu106
[UltraZed-EV Carrier]: https://www.xilinx.com/products/boards-and-kits/1-1s78dxb.html
<!-- updater end -->

#### Notes:
1. The Vivado Edition column indicates which designs are supported by the Vivado *Standard* Edition, the
   FREE edition which can be used without a license. Vivado *Enterprise* Edition requires
   a license however a 30-day evaluation license is available from the AMD Xilinx Licensing site.
2. The Stack Designs use the [M.2 M-key Stack FMC] with the [RPi Camera FMC] stacked on top of it. The non-stack
   designs use the [FPGA Drive FMC Gen4] on one FMC connector, and the [RPi Camera FMC] on another. This
   concept is best explained by the images below.
3. The `zcu106` target design uses the [FPGA Drive FMC Gen4] as the M.2 adapter for the Hailo-8.
   In that design, the [FPGA Drive FMC Gen4] connects to HPC1 while the [RPi Camera FMC] connects
   to the HPC0 connector.
4. The `zcu106_hpc0` and `uzev` target designs have support for 2x M.2 modules. To use the Hailo demo scripts,
   at least one of these modules must be the [Hailo-8 M.2 AI Acceleration Module]. The second slot can be used
   for a second Hailo module, or an NVMe SSD for storage.

### Stack vs Non-stack designs

| Stack design | Non-stack design |
|--------------|------------------|
| Requires [M.2 M-key Stack FMC] and uses only one FMC slot | Requires [FPGA Drive FMC Gen4] and uses two FMC slots |
| ![ZCU104 with camera and Hailo stack](docs/source/images/m2-mkey-stack-on-zcu104.jpg) | ![ZCU106 non-stack setup](https://www.fpgadeveloper.com/multi-camera-yolov5-on-zynq-ultrascale-with-hailo-8-ai-acceleration/images/zynqmp-hailo-ai-7.jpg) |

### M.2 Stack FMC PERST# control

On the [M.2 M-key Stack FMC], the PCIe reset (`PERST#`) of the two M.2 slots is driven
by a TCA9536 I2C I/O expander — output `[0]` is `PERST_A#` (M.2 slot A) and output `[1]`
is `PERST_B#` (M.2 slot B). The I2C bus that reaches this expander is routed to different
I/O on each carrier, so the mechanism used to drive `PERST#` is board-specific:

| Target design | FMC I2C routing | PERST# mechanism   | TCA9548 channel to TCA9536 |
|---------------|-----------------|--------------------|----------------------------|
| `uzev`        | PL I/O          | `pl_i2c` (AXI IIC) | n/a (direct AXI IIC)       |
| `zcu104`      | PS I/O (I2C1)   | `ps_i2c` (PS I2C1) | PCA9548 `0x74` → channel 5 |
| `zcu106_hpc0` | PS I/O (I2C1)   | `ps_i2c` (PS I2C1) | PCA9548 `0x75` → channel 0 (HPC0) |

On the `ps_i2c` boards the expander is reached through an on-board TCA9548 I2C switch on
`PS I2C1`, so the correct downstream mux channel (above) must be selected before accessing the
TCA9536 at `0x41`. Both the FSBL hook (early boot) and the device-tree `gpio-hog` (Linux)
select this path.

The base **`zcu106`** target does not use the M.2 Stack FMC — its M.2 adapter is the
[FPGA Drive FMC Gen4], whose `PERST#`/detect signals are on PL I/O and are handled by a
dedicated AXI GPIO instead (see [Hardware (Vivado)](#hardware-vivado)); no I2C expander is
involved.

## Build instructions

Clone the repo (with `--recursive` to pull the accelerator meta-layer submodules) and change
into its directory:
```
git clone --recursive https://github.com/AlbertaBeef/mb-edgeai-zynqmp.git
cd mb-edgeai-zynqmp
```

### Cross-platform build runner

All builds are driven by `build.py` at the repo root, on both Windows
(git bash) and Linux. The `build.sh` / `build.bat` shim finds a suitable
Python 3 automatically (including the one bundled with the AMD tools).
Pick a target design label from the tables above (or run `./build.sh
list`), then run the build command for the stage(s) you want — each
command builds whatever it depends on automatically and skips anything
already built. On Windows without git bash, run the same commands from
Command Prompt or PowerShell using `build.bat` (e.g. `build.bat xsa
--target <target>`).

You don't need to source the AMD tools first — the build runner finds
Vivado, Vitis and PetaLinux automatically in their standard install
locations and sets up the environment each stage needs. If your tools
are installed somewhere non-standard and the runner can't find them,
source the tool settings yourself before running the build.

#### Build the Vivado project (bitstream + XSA)

```
./build.sh xsa --target <target>
```

#### Build PetaLinux (Linux only)

```
./build.sh petalinux --target <target>
```

#### Build everything

Builds all of the above that the target supports, then gathers the boot
images into `bootimages/*.zip`:

```
./build.sh all --target <target>
./build.sh all --target all          # every target in the repo
```

Also available: `status`, `clean`, `project` — see
`./build.sh --help`. On Windows, the PetaLinux and Yocto stages require a
Linux machine; the runner says so and prints the hand-off command. The
legacy `make` interface still works on Linux (each Makefile now wraps
`build.sh`) but is deprecated and will be removed at the next version
update.

### Expected build time and disk usage

A first-time build runs roughly 14,000 bitbake tasks. With the public
Xilinx sstate-cache mirror reachable, most are pulled rather than
compiled locally — only the Hailo recipes (libhailort, hailortcli, the
TAPPAS post-processes) and the parts of the qt5 stack that depend on
them build from source.

On the reference build host used during development — Intel Xeon
E5-2640 v4 @ 2.40 GHz (40 cores), 503 GiB RAM, fast local disk — a
first-time build for a single target takes roughly **1 to 2 hours**
wall-clock with `JOBS=8`. Subsequent builds for the same target re-use
the local sstate and complete in 10–30 minutes. Building multiple
targets in parallel works well on a host this size; if you do, set
`JOBS=6` per build (passed to `make`) so the combined load stays
reasonable.

Disk: budget about **40–50 GiB** per target — most of that is
`PetaLinux/<target>/build/tmp/` plus the shared `downloads/`
directory.

On a smaller machine (8–16 cores, 32 GiB RAM is the practical
minimum), expect a from-scratch single-target build closer to **3 to
5 hours**.

### Build issue and workaround

When building the PetaLinux project, you might experience one or more of the following error messages:

```
ERROR: hailortcli-4.19.0-r0 do_configure: ExecutionError('/home/user/zynqmp-hailo-ai/PetaLinux/zcu106/build/tmp/work/cortexa72-cortexa53-xilinx-linux/hailortcli/4.19.0-r0/temp/run.do_configure.2849196', 1, None, None)
ERROR: Logfile of failure stored in: /home/user/zynqmp-hailo-ai/PetaLinux/zcu106/build/tmp/work/cortexa72-cortexa53-xilinx-linux/hailortcli/4.19.0-r0/temp/log.do_configure.2849196
ERROR: Task (/home/user/zynqmp-hailo-ai/PetaLinux/zcu106/project-spec/meta-user/meta-hailo/meta-hailo-libhailort/recipes-hailo/hailortcli/hailortcli_4.19.0.bb:do_configure) failed with exit code '1'
ERROR: libhailort-4.19.0-r0 do_configure: ExecutionError('/home/user/zynqmp-hailo-ai/PetaLinux/zcu106/build/tmp/work/cortexa72-cortexa53-xilinx-linux/libhailort/4.19.0-r0/temp/run.do_configure.2851680', 1, None, None)
ERROR: Logfile of failure stored in: /home/user/zynqmp-hailo-ai/PetaLinux/zcu106/build/tmp/work/cortexa72-cortexa53-xilinx-linux/libhailort/4.19.0-r0/temp/log.do_configure.2851680
ERROR: Task (/home/user/zynqmp-hailo-ai/PetaLinux/zcu106/project-spec/meta-user/meta-hailo/meta-hailo-libhailort/recipes-hailo/libhailort/libhailort_4.19.0.bb:do_configure) failed with exit code '1'
```

If you open one of the logfiles of those error messages, you will find error messages that are similar to the following:

```
Cloning into 'protobuf-src'...
fatal: unable to access 'https://github.com/protocolbuffers/protobuf.git/': error setting certificate file: /usr/local/oe-sdk-hardcoded-buildpath/sysroots/x86_64-petalinux-linux/etc/ssl/certs/ca-certificates.crt
```

#### Explanation:

In order to build the meta-hailo recipes, PetaLinux needs to clone some repositories.
To do this, it requires a digital certificate that it expects to find at
`/usr/local/oe-sdk-hardcoded-buildpath/sysroots/x86_64-petalinux-linux/etc/ssl/certs/ca-certificates.crt`.
That path is an OpenEmbedded eSDK relocation placeholder that PetaLinux is meant to
patch to the real location at install time, but on PetaLinux 2025.2 the relocation
does not fully fire for the certificate path baked into `git-native`'s libcurl.
This is a PetaLinux/eSDK packaging issue, not a Hailo or `meta-hailo` issue — a
from-source Yocto build does not hit it.

#### Work-around:

Create a symbolic link from the expected path to the host's system CA bundle so
the missing file is resolvable:

```
sudo mkdir -p /usr/local/oe-sdk-hardcoded-buildpath/sysroots/x86_64-petalinux-linux/etc/ssl/certs/
sudo ln -s /etc/ssl/certs/ca-certificates.crt /usr/local/oe-sdk-hardcoded-buildpath/sysroots/x86_64-petalinux-linux/etc/ssl/certs/ca-certificates.crt
```

After running the above commands, re-run the build with
`make clean TARGET=<board>` followed by `make petalinux TARGET=<board>` to
discard the cached failure.

## Contribute

We strongly encourage community contribution to these projects. Please make a pull request if you
would like to share your work:
* if you've spotted and fixed any issues
* if you've added designs for other target platforms
* if you've added software support for other cameras

Thank you to everyone who supports us!

### The TODO list

* Add some demo scripts for VCU

## About us

[Opsero Inc.](https://opsero.com "Opsero Inc.") is a team of FPGA developers delivering FPGA products and 
design services to start-ups and tech companies. Follow our blog, 
[FPGA Developer](https://www.fpgadeveloper.com "FPGA Developer"), for news, tutorials and
updates on the awesome projects we work on.

[FPGA Drive FMC Gen4]: https://docs.opsero.com/op063/datasheet/overview/
[M.2 M-key Stack FMC]: https://docs.opsero.com/op073/datasheet/overview/
[RPi Camera FMC]: https://docs.opsero.com/op068/datasheet/overview/
[Hailo-8 M.2 AI Acceleration Module]: https://hailo.ai/products/ai-accelerators/hailo-8-m2-ai-acceleration-module/

