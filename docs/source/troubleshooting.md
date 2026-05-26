# Troubleshooting

## Build failures

Check the following if the project fails to build or generate a bitstream:

1. **Are you using the correct version of Vivado for this version of the repository?**
   Check the version specified in the Requirements section of the README.md file.

2. **Did you follow the** [build instructions](build_instructions) **?**
   If it still doesn't build, please let us know and provide details of your setup and the error message(s).

3. **`libhailort` / `hailortcli` fail at `do_configure` with a CA-cert path
   under `/usr/local/oe-sdk-hardcoded-buildpath/...`.** This is the
   PetaLinux 2025.2 eSDK `git-native` CA-bundle relocation issue.
   Apply the one-time sudo symlink described in
   [Build issue and workaround](build_instructions.md#build-issue-and-workaround).
   `PetaLinux/Makefile` runs a `check_ca_workaround` prerequisite that
   prints the same fix if you haven't applied it yet.

4. **`bitbake petalinux-image-minimal failed` with `_setscene Fetcher failure`
   errors.** Transient public sstate mirror 404s. Re-run the same
   `make petalinux TARGET=<board>` command; the second attempt finds
   the packages in the local sstate cache populated by the first run.

## Boot-time issues

### SD init fails with `-110` on ZCU104

The Vivado design for this repo (and other Opsero ZynqMP designs)
exports a minimal `sdhci1` node into the XSA. The 2025.2 PetaLinux
device-tree generator does not fill in the bus-width / clock-frequency /
voltage properties this controller needs, and Linux times out with
-110 during SD card init. The ZCU104 BSP adds an explicit `sdhci1`
override block in `system-user.dtsi` that restores the properties the
stock AMD ZCU104 BSP carries. If you forked the BSP, copy that block
over.

### `hailo` driver WARN_ON in `find_vma()` (kernel ≥ 6.5)

The HailoRT v4.23 `hailo_pci` driver calls `find_vma()` without
holding `mmap_lock`, which kernel 6.5+ asserts on. The repo applies
the same patch that Hailo shipped on its v5.3-hotfix branch — see
`PetaLinux/bsp/<board>/project-spec/meta-user/recipes-kernel/hailo-pci/`.
If you see the WARN_ON, confirm the bbappend's `do_compile:prepend()`
hook ran (it logs `Applying hailo-pci mmap_lock patch for kernel >= 6.5`).

### Black screen on DisplayPort

The 2025.2 v_mix → DPSUB display pipeline requires
`xlnx_mixer.connect_drm_bridge=1` on the kernel command line. All
BSPs in this repo set it via `CONFIG_SUBSYSTEM_USER_CMDLINE`. If you
override the cmdline in a fork, ensure that flag is preserved or the
mixer probe will not bring the bridge up.

## Monitor resolution limited to 1080p

If using a 2K resolution DisplayPort monitor with the UltraZed EV carrier, you may find that
it can only be used at 1080p or less resolution. This is due to the fact that the UltraZed EV
carrier has only a single lane connected to the DisplayPort connector, and not all DisplayPort
monitors are able to support resolutions of 1080p and higher over a single lane.
