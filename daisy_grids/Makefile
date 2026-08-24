# Daisy Grids - Daisy Patch SM firmware
TARGET = sorrow

PROJECT_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
_P_DIR := $(patsubst %/,%,$(dir $(PROJECT_MAKEFILE)))
_P_UP1 := $(abspath $(_P_DIR)/..)
_P_UP2 := $(abspath $(_P_UP1)/..)
_P_UP3 := $(abspath $(_P_UP2)/..)
_P_UP4 := $(abspath $(_P_UP3)/..)
COMMON_MK := $(firstword \
	$(foreach d,$(_P_DIR) $(_P_UP1) $(_P_UP2) $(_P_UP3) $(_P_UP4),\
		$(if $(wildcard $(d)/make/common.mk),$(d)/make/common.mk,)))
ifeq ($(strip $(COMMON_MK)),)
$(error Could not find make/common.mk. Expected it in this repo; searched upwards from $(_P_DIR))
endif
include $(COMMON_MK)

# Board selection
USE_DAISY_PATCH_SM = 1

# FatFS, for reading a user drum-map bank off the SD card at boot.
USE_FATFS = $(SORROW_SD_USER_BANK)

CPP_SOURCES = \
	src/main.cpp \
	src/drum_voices.cpp \
	src/grids_port.cpp \
	src/grids_nodes.cpp \
	src/grids_nodes_groove.cpp \
	src/grids_nodes_club.cpp \
	src/grids_nodes_jazzlatin.cpp \
	src/speech_data.cpp

SORROW_VERSION ?= 2.3.1
C_DEFS += -DSORROW_VERSION=\"$(SORROW_VERSION)\"

# The user bank is read from the SD card at runtime, not compiled in, so there
# is one binary for everyone - see include/user_bank.h.
# Reading a user bank off the SD card is parked - it mounts but every read ends
# in FR_DISK_ERR after the driver's full 30 s timeout, and the cause is not yet
# found. See include/user_bank.h. Set SORROW_SD_USER_BANK=1 to build it back in.
SORROW_SD_USER_BANK ?= 0
C_DEFS += -DSORROW_SD_USER_BANK=$(SORROW_SD_USER_BANK)
ifeq ($(SORROW_SD_USER_BANK),1)
CPP_SOURCES += src/user_bank.cpp
endif

C_INCLUDES += \
	-I$(_P_DIR)/include \
	-I$(LIBDAISY_DIR)/src \
	-I$(DAISYSP_DIR)/Source

OPT = -O2

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# BOOT_SRAM: the app is loaded into SRAM by the Daisy bootloader from QSPI.
# Sorrow v2 needs this: the v1 BOOT_NONE image already filled 77% of the 128 KB
# internal flash, which leaves no room for the extra drum models. BOOT_SRAM
# raises the budget to ~480 KB and leaves QSPI writable at runtime.
#
# Requires the Daisy bootloader on the module (one-off: `make program-boot`).
# Override for a unit without it:  make APP_TYPE=BOOT_NONE
APP_TYPE ?= BOOT_SRAM

include $(LIBDAISY_DIR)/core/Makefile

# Ensure required static libraries exist
LIBDAISY_LIB ?= $(LIBDAISY_DIR)/build/libdaisy.a
DAISYSP_LIB  ?= $(DAISYSP_DIR)/build/libdaisysp.a

$(LIBDAISY_LIB):
	$(MAKE) -C $(LIBDAISY_DIR)

$(DAISYSP_LIB):
	$(MAKE) -C $(DAISYSP_DIR)


$(BUILD_DIR)/$(TARGET).elf: $(LIBDAISY_LIB) $(DAISYSP_LIB)


# DFU flashing helpers
DFU_UTIL   ?= dfu-util
# Daisy Patch SM (STM32 DFU) is typically 0483:df11.
DFU_VIDPID ?= 0483:df11
# If you have more than one STM32 in DFU mode, set DFU_SERIAL to pick the right one.
DFU_SERIAL ?=
DFU_ALT    ?= 0
# Follows APP_TYPE via libDaisy: internal flash (0x08000000) for BOOT_NONE,
# QSPI (0x90040000) for BOOT_SRAM/BOOT_QSPI.
DFU_ADDR   ?= $(FLASH_ADDRESS):leave

.PHONY: dfu-list dfu-check program-dfu-local flash

dfu-list:
	@$(DFU_UTIL) -l

dfu-check:
	@set -e; \
	serials=`$(DFU_UTIL) -l 2>/dev/null \
		| awk -F'serial="' '/Found DFU: \['"$(DFU_VIDPID)"'\]/{split($$2,a,"\""); print a[1]}' \
		| sort -u`; \
	count=`printf "%s\n" "$$serials" | sed '/^$$/d' | wc -l | tr -d ' '`; \
	if [ "$$count" -eq 0 ]; then \
		echo "No DFU device found for $(DFU_VIDPID). Put the Patch SM into DFU mode and try again."; \
		echo "Tip: run 'make dfu-list' to see all DFU devices."; \
		exit 2; \
	fi; \
	if [ "$$count" -gt 1 ] && [ -z "$(DFU_SERIAL)" ]; then \
		echo "More than one DFU device matched $(DFU_VIDPID)."; \
		echo "Re-run with: make flash DFU_SERIAL=<serial>"; \
		echo "Available serials:"; \
		printf "  %s\n" $$serials; \
		exit 2; \
	fi

program-dfu-local: build/$(TARGET).bin
	@set -e; \
	$(MAKE) dfu-check; \
	serial_to_use="$(DFU_SERIAL)"; \
	if [ -z "$$serial_to_use" ]; then \
		serial_to_use=`$(DFU_UTIL) -l 2>/dev/null \
			| awk -F'serial="' '/Found DFU: \['"$(DFU_VIDPID)"'\]/{split($$2,a,"\""); print a[1]}' \
			| sort -u | sed -n '1p'`; \
	fi; \
	if [ -z "$$serial_to_use" ]; then \
		echo "Could not determine DFU serial automatically. Use: make flash DFU_SERIAL=<serial>"; \
		exit 2; \
	fi; \
	out=`$(DFU_UTIL) -d $(DFU_VIDPID) -S $$serial_to_use -a $(DFU_ALT) -s $(DFU_ADDR) -D build/$(TARGET).bin 2>&1` || status=$$?; \
	status=$${status:-0}; \
	echo "$$out"; \
	if [ $$status -eq 74 ]; then \
		echo "$$out" | grep -Eq "Download[[:space:]]+\[|Download done|File downloaded successfully" && { \
			echo "dfu-util returned 74 on :leave; firmware was written OK. Tap RESET."; \
			exit 0; \
		}; \
	fi; \
	exit $$status

flash: program-dfu-local

#######################################
# Release
#######################################
# Builds the distributable firmware: the four factory banks, version stamped
# into the binary and the filename. Distributing this is distributing a
# GPL-3.0-or-later work, so the corresponding source has to be available - tag
# the commit you build from and publish the tag alongside the binary.
#
#   make release                     # -> build/sorrow-v2.2.0.bin
#   make release SORROW_VERSION=2.2.0

.PHONY: release
release:
	@$(MAKE) clean >/dev/null
	@$(MAKE) SORROW_VERSION=$(SORROW_VERSION) >/dev/null
	@cp build/$(TARGET).bin build/$(TARGET)-v$(SORROW_VERSION).bin
	@echo "build/$(TARGET)-v$(SORROW_VERSION).bin  ($$(stat -f%z build/$(TARGET)-v$(SORROW_VERSION).bin) bytes)"
	@echo "  four factory banks (Original / Club / Traditional / Latin)"
	@echo "  requires the Daisy bootloader on the target: make program-boot"
	@echo "  tag the source before publishing: git tag sorrow-v$(SORROW_VERSION)"

#######################################
# SD card flashing
#######################################
# With the Daisy bootloader installed, the most convenient way to load a build
# is to drop the .bin on the SD card and power-cycle: the bootloader scans the
# card root for the first .bin and re-flashes QSPI only when it differs, so the
# file can just live there and be replaced to iterate.
#
# Requires a BOOT_SRAM/BOOT_QSPI build — a BOOT_NONE .bin on the card makes the
# bootloader blink SOS.
#
# The copy is verified and the card is then ejected automatically: macOS buffers
# writes to removable media, and `sync` alone does not reliably flush them, so
# pulling the card without ejecting can leave a truncated .bin on it - which the
# bootloader will happily flash.
#
# BANK=<file> also copies a user drum-map bank to the card root, as
# sorrow_bank.dat. The extension matters: the bootloader flashes the first .bin
# it finds in the root, so anything that is not firmware must not be named .bin.
# The AppleDouble companions macOS leaves behind are removed for the same reason
# - ._something.bin is a .bin as far as a directory scan is concerned.
#
#   make card                            # copy to the single mounted FAT volume
#   make card BANK=~/sorrow_bank.dat     # firmware plus a user bank
#   make card SD_VOLUME=/Volumes/DAISY   # or name it explicitly
#   make card NO_EJECT=1                 # copy but leave the card mounted
#   make card-list                       # show candidate volumes

SD_VOLUME ?=

# FAT volumes mounted under /Volumes (macOS). exFAT is listed too so the target
# can explain itself, but the bootloader wants FAT32.
_SD_FIND = mount -t msdos 2>/dev/null | sed -n 's|^.* on \(/Volumes/.*\) (.*|\1|p'
_SD_FIND_EXFAT = mount -t exfat 2>/dev/null | sed -n 's|^.* on \(/Volumes/.*\) (.*|\1|p'

.PHONY: card card-list

card-list:
	@echo "FAT32 volumes (usable):"; \
	$(_SD_FIND) | sed 's/^/  /' | grep . || echo "  (none)"; \
	echo "exFAT volumes (reformat as FAT32 to use):"; \
	$(_SD_FIND_EXFAT) | sed 's/^/  /' | grep . || echo "  (none)"

card: build/$(TARGET).bin
	@set -e; \
	if [ "$(APP_TYPE)" = "BOOT_NONE" ]; then \
		echo "APP_TYPE=BOOT_NONE builds cannot boot from SD (the bootloader will blink SOS)."; \
		echo "Build without the override, or use 'make flash' for a bootloader-less unit."; \
		exit 2; \
	fi; \
	vol="$(SD_VOLUME)"; \
	if [ -z "$$vol" ]; then \
		cands=`$(_SD_FIND)`; \
		count=`printf "%s\n" "$$cands" | sed '/^$$/d' | wc -l | tr -d ' '`; \
		if [ "$$count" -eq 0 ]; then \
			echo "No FAT32 volume mounted. Insert the SD card, or pass SD_VOLUME=/Volumes/NAME."; \
			exfat=`$(_SD_FIND_EXFAT)`; \
			if [ -n "$$exfat" ]; then \
				echo "Found exFAT volume(s) — the bootloader needs FAT32:"; \
				printf "  %s\n" $$exfat; \
			fi; \
			exit 2; \
		fi; \
		if [ "$$count" -gt 1 ]; then \
			echo "More than one FAT32 volume is mounted. Re-run with SD_VOLUME=<path>:"; \
			printf "%s\n" "$$cands" | sed 's/^/  /'; \
			exit 2; \
		fi; \
		vol="$$cands"; \
	fi; \
	if [ ! -d "$$vol" ]; then echo "Not a mounted volume: $$vol"; exit 2; fi; \
	others=`ls "$$vol"/*.bin 2>/dev/null \
		| grep -v "/$(TARGET).bin$$" || true`; \
	if [ -n "$$others" ]; then \
		mkdir -p "$$vol/_firmware"; \
		echo "Other firmware .bin files were at the card root. The bootloader"; \
		echo "flashes the first .bin it finds, so on a shared card that would load"; \
		echo "the wrong firmware into this module. Moved aside (still on card):"; \
		for f in $$others; do \
			mv "$$f" "$$vol/_firmware/"; \
			echo "  `basename $$f`  ->  _firmware/`basename $$f`"; \
		done; \
		echo "To re-arm one of those, move it back to the card root."; \
	fi; \
	cp build/$(TARGET).bin "$$vol/$(TARGET).bin"; \
	if [ -n "$(BANK)" ]; then \
		if [ ! -f "$(BANK)" ]; then echo "No such bank file: $(BANK)"; exit 2; fi; \
		cp "$(BANK)" "$$vol/sorrow_bank.dat"; \
		echo "Copied `basename $(BANK)` -> $$vol/sorrow_bank.dat"; \
	fi; \
	find "$$vol" -maxdepth 1 -name '._*' -delete 2>/dev/null || true; \
	sync; \
	if ! cmp -s build/$(TARGET).bin "$$vol/$(TARGET).bin"; then \
		echo "Verify FAILED: the copy on the card does not match the build."; \
		exit 1; \
	fi; \
	echo "Copied and verified $(TARGET).bin -> $$vol"; \
	if [ -z "$(NO_EJECT)" ] && command -v diskutil >/dev/null 2>&1; then \
		diskutil eject "$$vol" >/dev/null 2>&1 \
			&& echo "Card ejected - safe to remove." \
			|| echo "Could not eject automatically; eject before removing the card."; \
	else \
		echo "Eject before removing the card ('sync' alone does not flush removable media)."; \
	fi; \
	echo "Put it in the module and power-cycle."
