FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://xfsbl_hooks.c"
  
# fsbl-firmware sources are hardlink-copied from the shared embeddedsw tree into
# ${S} by do_copy_shared_src (after do_patch, before do_configure). Install our
# PERST# hook here, AFTER that copy. Must be `install`, NOT `cp -f`: ${S} files
# are hardlinks to work-shared/embeddedsw-*/git; an in-place overwrite would
# corrupt the source shared by every esw recipe. install removes-then-creates.
do_configure:prepend() {
    install -m 0644 ${WORKDIR}/xfsbl_hooks.c \
        ${S}/lib/sw_apps/zynqmp_fsbl/src/xfsbl_hooks.c
}
