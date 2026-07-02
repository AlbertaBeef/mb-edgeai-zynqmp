# ZCU104 FSBL VADJ fix.
#
# The stock 2025.2 FSBL does not enable VADJ correctly on the ZCU104: it reads
# the VADJ record from the board EEPROM (0x54) instead of the FMC EEPROM (0x50),
# selects the wrong I2C-MUX channel, and reads only 32 bytes (too few to reach
# the VADJ record). These pre-patched xfsbl_board.c / xfsbl_board.h fix all three.
#
# NOTE: 2025.2's xlnx-embeddedsw.bbclass hardlink-copies the fsbl-firmware
# sources from the shared embeddedsw tree into ${S} by do_copy_shared_src (which
# runs AFTER do_patch, on an otherwise-empty workdir). So we install our
# replacements in do_configure:prepend, AFTER that copy. Must be `install`, NOT
# `cp -f`: the ${S} files are hardlinks to work-shared/embeddedsw-*/git, and an
# in-place overwrite would corrupt the source shared by every esw recipe.
# install removes-then-creates, which breaks the hardlink safely.

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://xfsbl_board.c file://xfsbl_board.h"

do_configure:prepend() {
    install -m 0644 ${WORKDIR}/xfsbl_board.c \
        ${S}/lib/sw_apps/zynqmp_fsbl/src/xfsbl_board.c
    install -m 0644 ${WORKDIR}/xfsbl_board.h \
        ${S}/lib/sw_apps/zynqmp_fsbl/src/xfsbl_board.h
}
