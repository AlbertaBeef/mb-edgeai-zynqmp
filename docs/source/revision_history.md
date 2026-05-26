# Revision History

## 2025.2 Changes

* Migrated all designs and PetaLinux projects to Vivado / Vitis / PetaLinux 2025.2
* Moved to the SDT (System Device Tree) flow for PetaLinux configuration
* HailoRT and `hailo_pci` updated to 4.23.0; backported the `find_vma()` /
  `mmap_read_lock` fix as a kernel ≥ 6.5 hotfix for the v4.23 driver
* Added three Xilinx DRM kernel patches required when v_mix runs in
  DRM-bridge mode (mixer NULL-deref, double-cleanup on unbind, vblank
  shutdown ordering); kernel cmdline now passes
  `xlnx_mixer.connect_drm_bridge=1`
* `meta-hailo` is now consumed as a git submodule tracking the
  `hailo8-scarthgap` branch and copied into each project at configure
  time; `packagegroup-hailo-tappas` no longer pulls in the removed
  `tappas-apps` legacy recipe
* `LICENSE_PATH` is rewritten from `=` to `+=` in the project-local copy
  of `meta-hailo-tappas/conf/layer.conf` so `meta-qt5`'s licenses
  survive (qtbase otherwise fails `do_create_spdx`)
* Added a `check_ca_workaround` Make target that fails fast with the
  one-time sudo symlink fix for the PetaLinux 2025.2 eSDK
  `git-native` CA-bundle relocation issue
* ZCU104 BSP adds an explicit `sdhci1` device-tree override
  (Opsero ZynqMP designs export a minimal sdhci1 node; without the
  override SD init fails with -110)
* UltraZed-EV BSP forces PMUFW/FSBL/TF-A/Linux serial to PSU_UART_0
  (gen-machine-conf defaults to UART_1 on this carrier in 2025.2)

## 2024.1 Changes

* Removed VVAS multiscaler accelerator from all designs (Xilinx has no support for 2024.1)
* Added AXI4-Streaming Data FIFO to MIPI video pipes, between MIPI CSI2 RX and ISP Pipeline IPs
* Improved documentation, centralized target design info to JSON file
* RPi camera IOs are properly driven
* ISP Pipeline IP updated to version v2023.2_update1 of the Vitis_Libraries repo
* ISP Pipeline now uses built-in Linux driver (linux-xlnx/drivers/media/platform/xilinx/xilinx-isppipeline.c)
* Removed all Vitis-AI and VVAS recipes from BSPs
* Improved example scripts: use max res of connected display, display only connected cameras

## 2022.1 Changes

* First revision

