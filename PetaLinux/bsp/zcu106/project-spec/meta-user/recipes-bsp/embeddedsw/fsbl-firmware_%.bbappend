# ZCU106 (base "zcu106" target) FSBL: FPGA Drive FMC PERST# reset pulse.
#
# xfsbl_hooks.c cycles PERST_A#/PERST_B# of the FPGA Drive FMC Gen4 (on HPC1) via
# the fpga_drive_gpio AXI GPIO, so a slow-to-train endpoint is up before Linux
# probes the PCIe root complex. Unlike the "zcu106_hpc0" target (M.2 M-key Stack
# FMC, TCA9536 over PS I2C1 -- see bsp/zcu106_hpc0), PERST# here is on PL I/O, so
# the FSBL drives a memory-mapped AXI GPIO rather than walking the I2C mux tree.
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
