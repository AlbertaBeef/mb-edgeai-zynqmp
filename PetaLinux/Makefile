# Opsero Electronic Design Inc. 2024
#
# This Makefile can be used to build the PetaLinux projects.

RM = rm -rf
PETL_ROOT = $(shell pwd)
BD_NAME = rpi

# defaults
.DEFAULT_GOAL := petalinux
TARGET ?= none
JOBS ?= 8

# valid targets (template name, flash size, flash interface)
# UPDATER START
zcu104_target := zynqMP 0 0 no_accel
zcu106_target := zynqMP 0 0 no_accel
zcu106_hpc0_target := zynqMP 0 0 no_accel
pynqzu_target := zynqMP 0 0 no_accel
uzev_target := zynqMP 0 0 no_accel
# UPDATER END

TARGET_LIST := $(sort $(patsubst %_target,%,$(filter %_target,$(.VARIABLES))))

# target board (the first word in the target name, before the first underscore)
TARGET_BOARD=$(word 1,$(subst _, ,$(TARGET)))

# Vivado paths
VIV_DIR = $(PETL_ROOT)/../Vivado
VIV_PRJ_DIR = $(VIV_DIR)/$(TARGET)
VIV_XSA = $(VIV_PRJ_DIR)/$(BD_NAME)_wrapper.xsa

# Vitis Accel paths
VIT_ACC_DIR = $(PETL_ROOT)/../VitisAccel

# petalinux paths and files
PETL_DIR = $(PETL_ROOT)/$(TARGET)
PETL_IMG_DIR = $(PETL_DIR)/images/linux
PETL_BL31_ELF = $(PETL_IMG_DIR)/bl31.elf
PETL_PMUFW_ELF = $(PETL_IMG_DIR)/pmufw.elf
PETL_ZYNQMP_FSBL_ELF = $(PETL_IMG_DIR)/zynqmp_fsbl.elf
PETL_ZYNQ_FSBL_ELF = $(PETL_IMG_DIR)/zynq_fsbl.elf
PETL_FSBOOT_ELF = $(PETL_IMG_DIR)/fs-boot.elf
PETL_UBOOT_ELF = $(PETL_IMG_DIR)/u-boot.elf
PETL_DTB = $(PETL_IMG_DIR)/system.dtb
PETL_BOOT_BIN = $(PETL_IMG_DIR)/BOOT.BIN
PETL_BOOT_SCR = $(PETL_IMG_DIR)/boot.scr
PETL_BOOT_MCS = $(PETL_IMG_DIR)/boot.mcs
PETL_ROOTFS = $(PETL_IMG_DIR)/rootfs.tar.gz
PETL_IMAGE_UB = $(PETL_IMG_DIR)/image.ub
PETL_CFG_DONE = $(PETL_DIR)/configdone.txt
PETL_COMP_DIR = $(PETL_DIR)/components
PETL_OFFLINE = $(PETL_ROOT)/offline.txt
PETL_LOCK = $(PETL_ROOT)/.$(TARGET).lock

# Hailo recipes
SUBMOD_PATH = $(PETL_ROOT)/../submodules
# Accelerator meta-layer submodules. Copied into <target>/project-spec/ and
# registered as bitbake layers via CONFIG_USER_LAYER_* in project-spec/configs/config.
ACCEL_RECIPES = $(SUBMOD_PATH)/meta-hailo \
                $(SUBMOD_PATH)/meta-axelera \
                $(SUBMOD_PATH)/meta-deepx-m1 \
                $(SUBMOD_PATH)/memx-yocto

# These macros return values from the valid target lists defined above
define get_template_name
$(word 1,$($(1)_target))
endef

define get_flash_size
$(word 2,$($(1)_target))
endef

define get_flash_intf
$(word 3,$($(1)_target))
endef

define get_has_accel
$(word 4,$($(1)_target))
endef

# For offline PetaLinux builds
SSTATE_PATH ?= $(shell test -e $(PETL_OFFLINE) && head -n 1 $(PETL_OFFLINE))

ifeq ($(call get_template_name,$(TARGET)), microblaze)
	SSTATE_ARCH = microblaze
	BUILD_PRODUCTS = $(PETL_FSBOOT_ELF) $(PETL_UBOOT_ELF) $(PETL_DTB)
	PKG_PRODUCTS = $(PETL_BOOT_MCS)
	PKG_CMD = petalinux-package --boot --force --fpga $(VIV_BIT) --u-boot --kernel --flash-size $(call get_flash_size,$(TARGET)) --flash-intf $(call get_flash_intf,$(TARGET))
else ifeq ($(call get_template_name,$(TARGET)), zynq)
	SSTATE_ARCH = arm
	BUILD_PRODUCTS = $(PETL_ZYNQ_FSBL_ELF) $(PETL_UBOOT_ELF) $(PETL_DTB)
	PKG_PRODUCTS = $(PETL_BOOT_BIN)
	PKG_CMD = petalinux-package --boot --fsbl ./images/linux/zynq_fsbl.elf --fpga $(VIV_BIT) --u-boot
else ifeq ($(call get_template_name,$(TARGET)), zynqMP)
	SSTATE_ARCH = aarch64
	BUILD_PRODUCTS = $(PETL_BL31_ELF) $(PETL_PMUFW_ELF) $(PETL_ZYNQMP_FSBL_ELF) $(PETL_UBOOT_ELF) $(PETL_DTB)
	PKG_PRODUCTS = $(PETL_BOOT_BIN)
	PKG_CMD = petalinux-package --boot --fsbl ./images/linux/zynqmp_fsbl.elf --fpga $(VIV_BIT) --u-boot
endif

ifeq ($(call get_has_accel,$(TARGET)), accel)
	VIV_BIT = $(VIT_ACC_DIR)/$(TARGET)_container/link/int/system.bit
	MAKE_BIT_CMD = $(MAKE) -C $(VIT_ACC_DIR) xclbin TARGET=$(TARGET) JOBS=$(JOBS)
else
	VIV_BIT = $(VIV_PRJ_DIR)/$(TARGET).runs/impl_1/$(BD_NAME)_wrapper.bit
	MAKE_BIT_CMD = @echo 'Vivado failed to generate the bitstream'
endif

ifneq ($(SSTATE_PATH),)
	MIRROR_URL = CONFIG_PRE_MIRROR_URL="file://$(SSTATE_PATH)/downloads"
	SSTATE_FEEDS = CONFIG_YOCTO_LOCAL_SSTATE_FEEDS_URL="$(SSTATE_PATH)/$(SSTATE_ARCH)"
endif

.PHONY: help
help:
	@echo 'Usage:'
	@echo ''
	@echo '  make petalinux TARGET=<val> JOBS=<val>'
	@echo '    Build the PetaLinux project for specified target.'
	@echo ''
	@echo '  make all JOBS=<val>'
	@echo '    Build the PetaLinux project for all target designs.'
	@echo ''
	@echo '  make clean TARGET=<val>'
	@echo '    Delete PetaLinux project of specified target.'
	@echo ''
	@echo '  make clean_all'
	@echo '    Delete PetaLinux projects of all targets.'
	@echo ''
	@echo '  make status TARGET=<val>'
	@echo '    Print the status (built,packaged) of the PetaLinux project of specified target.'
	@echo ''
	@echo '  make status_all'
	@echo '    Print status of the PetaLinux projects of all targets.'
	@echo ''
	@echo 'Parameters:'
	@echo ''
	@echo '  TARGET: Name of the target design, must be one of the following:'
	@$(foreach targ,$(TARGET_LIST),echo "    - $(targ)";)
	@echo ''
	@echo '  JOBS: Optional param to set number of synthesis jobs (default 8)'
	@echo ''
	@echo 'Example usage:'
	@echo '  make petalinux TARGET=$(word 1,$(TARGET_LIST))'
	@echo ''

.PHONY: petalinux
petalinux: check_target check_ca_workaround
	@if [ -f $(PETL_LOCK) ]; then \
		echo "$(TARGET) is locked. Skipping..."; \
	else \
		touch $(PETL_LOCK); \
		$(MAKE) --no-print-directory petalinux_locked TARGET=$(TARGET) JOBS=$(JOBS); \
		rm -f $(PETL_LOCK); \
	fi

petalinux_locked: package

package: $(PKG_PRODUCTS)
$(PKG_PRODUCTS): $(BUILD_PRODUCTS) $(VIV_BIT)
	@echo 'Packaging PetaLinux project for $(TARGET)'
	cd $(TARGET) && $(PKG_CMD)
	
# Generate bitstream (should already be done unless target uses an accelerator)
$(VIV_BIT): 
	$(MAKE_BIT_CMD)

PETL_BUILD_DEPS = $(PETL_FSBOOT_ELF) $(PETL_UBOOT_ELF) $(PETL_DTB) \
                  $(PETL_ZYNQ_FSBL_ELF) \
                  $(PETL_BL31_ELF) $(PETL_PMUFW_ELF) $(PETL_ZYNQMP_FSBL_ELF)

$(PETL_BUILD_DEPS): $(PETL_CFG_DONE)
	cd $(TARGET) && petalinux-build

$(PETL_CFG_DONE): | $(PETL_COMP_DIR)
	cp -R ./bsp/$(TARGET_BOARD)/project-spec ./$(TARGET)/.
	-cp -R ./bsp/$(TARGET)/project-spec ./$(TARGET)/.
	-cp -R $(ACCEL_RECIPES) ./$(TARGET)/project-spec/.
	# Upstream meta-hailo-tappas/conf/layer.conf uses LICENSE_PATH = (hard
	# assignment), wiping out additions from prior layers (meta-qt5,
	# meta-openembedded). qtbase then fails do_create_spdx with
	# "Cannot find any text for license The-Qt-Company-GPL-Exception-1.0".
	# Convert to += in the project-local copy. The submodule itself is
	# left untouched so `git submodule update` doesn't undo this.
	-sed -i 's|^LICENSE_PATH = |LICENSE_PATH += |' \
	    ./$(TARGET)/project-spec/meta-hailo/meta-hailo-tappas/conf/layer.conf
	@if [ ! -z "$(SSTATE_PATH)" ]; then \
		echo '$(MIRROR_URL)' >> ./$(TARGET)/project-spec/configs/config; \
		echo '$(SSTATE_FEEDS)' >> ./$(TARGET)/project-spec/configs/config; \
		echo 'Configuring project for offline build ($(SSTATE_PATH))'; \
	fi
	touch $(TARGET)/configdone.txt
	cd $(TARGET) && petalinux-config --silentconfig
	
$(PETL_COMP_DIR): | $(PETL_DIR)
	cd $(TARGET) && petalinux-config --get-hw-description $(VIV_XSA) --silentconfig
	
$(PETL_DIR): $(VIV_XSA)
	@if [ -d $@ ]; then echo "Project $(TARGET) already exists but is outdated. Use 'make clean TARGET=$(TARGET)' to remove it."; exit 1; fi
	petalinux-create --type project --template $(call get_template_name,$(TARGET)) --name $(TARGET)

$(VIV_XSA):
	$(MAKE) -C $(VIV_DIR) xsa TARGET=$(TARGET) JOBS=$(JOBS)

.PHONY: all
all:
	@{ \
	for targ in $(TARGET_LIST); do \
		$(MAKE) --no-print-directory petalinux TARGET=$${targ} JOBS=$(JOBS); \
	done; \
	}

.PHONY: clean
clean: check_target
	$(RM) $(TARGET)

.PHONY: clean_all
clean_all:
	for targ in $(TARGET_LIST); do \
		$(MAKE) --no-print-directory clean TARGET=$$targ; \
	done

.PHONY: status
status: check_target
	@{ \
	all_built=true; \
	for file in $(BUILD_PRODUCTS); do \
		if [ ! -e $$file ]; then \
			printf 'Target %-20s: **NOT BUILT** & ' "$(TARGET)"; \
			all_built=false; \
			break; \
		fi; \
	done; \
	if $$all_built; then \
		printf 'Target %-20s: BUILT & ' "$(TARGET)"; \
	fi; \
	all_built=true; \
	for file in $(PKG_PRODUCTS); do \
		if [ ! -e $$file ]; then \
			printf '**NOT PACKAGED**\n'; \
			all_built=false; \
			break; \
		fi; \
	done; \
	if $$all_built; then \
		printf 'PACKAGED\n'; \
	fi; \
	}

.PHONY: status_all
status_all:
	@{ \
	for targ in $(TARGET_LIST); do \
		$(MAKE) --no-print-directory status TARGET=$$targ; \
	done; \
	}

check_target:
ifndef $(TARGET)_target
	$(error "Please specify a TARGET. Use 'make help' to see valid targets.")
endif

# >>> HAILO-SPECIFIC (do not drop when syncing this Makefile from other repos) <<<
#
# PetaLinux 2025.2 ships an eSDK whose git-native has an unrelocated CA-cert
# path baked into libcurl. libhailort's CMake FetchContent_Declare clones at
# do_configure time and fails on that path. The README documents a one-time
# sudo workaround (symlink). This target fails fast with a clear message if
# the workaround has not been applied yet.
.PHONY: check_ca_workaround
check_ca_workaround:
	@if [ ! -e /usr/local/oe-sdk-hardcoded-buildpath/sysroots/x86_64-petalinux-linux/etc/ssl/certs/ca-certificates.crt ]; then \
		echo ""; \
		echo "ERROR: PetaLinux 2025.2 host CA workaround not applied."; \
		echo ""; \
		echo "libhailort's CMake clones dependencies at do_configure, using a"; \
		echo "git-native whose CA path is unrelocated on PetaLinux 2025.2. Without"; \
		echo "the workaround, the build will fail at libhailort:do_configure."; \
		echo ""; \
		echo "Run these once on this host (requires sudo), then re-run make:"; \
		echo ""; \
		echo "  sudo mkdir -p /usr/local/oe-sdk-hardcoded-buildpath/sysroots/x86_64-petalinux-linux/etc/ssl/certs"; \
		echo "  sudo ln -s /etc/ssl/certs/ca-certificates.crt /usr/local/oe-sdk-hardcoded-buildpath/sysroots/x86_64-petalinux-linux/etc/ssl/certs/ca-certificates.crt"; \
		echo ""; \
		echo "Full explanation in README.md, section 'Build issue and workaround'."; \
		echo ""; \
		exit 1; \
	fi


