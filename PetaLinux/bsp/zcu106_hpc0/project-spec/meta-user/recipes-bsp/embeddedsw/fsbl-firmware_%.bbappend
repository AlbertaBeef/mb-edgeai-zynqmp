# ZCU106 (zcu106_hpc0 target) FSBL: M.2 Stack FMC PERST# assertion.
#
# xfsbl_hooks.c cycles PERST_A#/PERST_B# of the M.2 Stack FMC (TCA9536
# I/O-expander) so slow-to-train endpoints are up before Linux probes the PCIe
# root complex. On the ZCU106 the FMC HPC0 I2C is on the PS (I2C1 -> PCA9548
# U135 @ 0x75 -> channel 0 -> TCA9536 @ 0x41), a different path than the ZCU104.
# This applies to the "zcu106_hpc0" target only (M.2 M-key Stack FMC on HPC0).
# The base "zcu106" target uses the FPGA Drive FMC and a different FSBL; see
# bsp/zcu106.
#
# Unlike the ZCU104, no xfsbl_board.c/.h override is needed here -- the stock
# 2025.2 FSBL already handles ZCU106 VADJ.
#
# NOTE: 2025.2's xlnx-embeddedsw.bbclass hardlink-copies the fsbl-firmware
# sources from the shared embeddedsw tree into ${S} by do_copy_shared_src (which
# runs AFTER do_patch, on an otherwise-empty workdir). So we install our
# replacement in do_configure:prepend, AFTER that copy. Must be `install`, NOT
# `cp -f`: the ${S} files are hardlinks to work-shared/embeddedsw-*/git, and an
# in-place overwrite would corrupt the source shared by every esw recipe.

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://xfsbl_hooks.c"

do_configure:prepend() {
    install -m 0644 ${WORKDIR}/xfsbl_hooks.c \
        ${S}/lib/sw_apps/zynqmp_fsbl/src/xfsbl_hooks.c
}
