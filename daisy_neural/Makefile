# Project Name
TARGET = daisy_neural

# Library Locations
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

LIBDAISY_DIR ?= $(DAISY_ROOT)/libDaisy
DAISYSP_DIR  ?= $(DAISY_ROOT)/DaisySP

# Build directory
BUILD_DIR ?= build

# Sources
CPP_SOURCES = src/main.cpp \
              src/capture_store.cpp \
              src/seed_weights.cpp \
              $(_P_UP1)/common/oled_soft_i2c.cpp

# FatFS long-filename support. libDaisy compiles ff.c with LFN enabled but does
# not build the code-page table it needs, so anything that walks a directory
# (f_readdir -> get_fileinfo -> ff_convert/ff_wtoupper) fails to link. ccsbcs.c
# is the single-byte code-page table that supplies both.
C_SOURCES += $(LIBDAISY_DIR)/Middlewares/Third_Party/FatFs/src/option/ccsbcs.c

# Additional include paths
C_INCLUDES += -I$(_P_UP1)/common
# Project root, so src/main.cpp can include the lifted engine as "nam/...".
C_INCLUDES += -I$(_P_DIR)

# The lifted A2 runtime uses inline variables, which are C++17. GCC accepts
# them at the core Makefile's gnu++14 default as an extension, but warns
# fifteen times a build — noise that would hide a real warning. The core sets
# CPP_STANDARD with ?=, so setting it here takes precedence. Only our own
# translation units are affected; libdaisy.a is prebuilt.
CPP_STANDARD = -std=gnu++17

# Linker configuration
LDFLAGS += -u _printf_float

# Core location and generic makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# App type / linker selection.
#
# BOOT_SRAM, which is what the engine was written for: the hot weights go to
# DTCMRAM and the ~76 KB history to the otherwise-empty RAM_D2, both on-chip.
# It needs the Daisy bootloader on the unit — a one-off, and on a fresh
# patch.init() there is no reason not to. It also lifts the 128 KB internal
# flash ceiling that the BOOT_NONE build was already 84.5% into.
#
# BOOT_NONE still works and still needs no bootloader, which is worth keeping
# for a first-power-on sanity check before the bootloader is installed:
#   make APP_TYPE=BOOT_NONE
# In that mode the engine's placement sections do not exist, so the placement
# macros are neutralised and everything lands in ordinary .bss — correct, just
# slower, since the history ends up in AXI SRAM rather than RAM_D2.
APP_TYPE ?= BOOT_SRAM

ifeq ($(APP_TYPE),BOOT_NONE)
C_DEFS += -DNEURAL_NO_TCM_PLACEMENT
else
# Supplemental fragment putting the ~76 KB A2 history in RAM_D2 instead of
# crowding DTCMRAM. It must be FIRST on the link line: ld treats the LAST -T as
# the main script, so passing it after libDaisy's makes ld use it as the main
# script and the INSERT anchor disappears. LDFLAGS set here is emitted before
# the core Makefile's -T$(LDSCRIPT), which is what we want.
#
# Verify by the region table, not by a clean build: a failed placement still
# links and still boots, it just silently lands in DTCMRAM.
LDFLAGS += -T$(_P_DIR)/nam/neural_a2_sections.lds
endif

ifeq ($(APP_TYPE),BOOT_QSPI)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_qspi.lds
else ifeq ($(APP_TYPE),BOOT_SRAM)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_sram.lds
else
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_flash.lds
endif
include $(SYSTEM_FILES_DIR)/Makefile

#######################################
# NAM A2 memory placement
#######################################
# nam/nam_a2_sections.lds adds a .sram_d2_bss output section in RAM_D2 so the
# ~76 KB history buffer lives there instead of crowding DTCMRAM.
#
# It must come AFTER libDaisy's main script on the command line, which is why
# this append sits below the include rather than above it. The fragment's own
# header says to put it first, per the ld manual's rule for INSERT scripts —
# but with this toolchain (ld 12.3.1) that produces
#   warning: memory region `RAM_D2' not declared
# and the section falls back into DTCMRAM, which is a silent failure: it links
# clean, boots, and is merely slower and nearly out of DTCM. Measured: first
# 105,344 B of 128 KB DTCMRAM used and RAM_D2 at zero; after, the history is in
# RAM_D2 where it belongs. Check both regions in the build output if this is
# ever touched.
#######################################
# DFU flashing
#######################################
# Same pattern as daisy_grids, including its two hard-won details: a readable
# error when no DFU device is present, and the dfu-util quirk where a successful
# write to a ":leave" address still exits 74.
#
#   make flash                        build (if needed) and flash over DFU
#   make dfu-list                     show every DFU device attached
#   make flash DFU_SERIAL=<serial>    pick one when several are in DFU mode
DFU_UTIL   ?= dfu-util
# Daisy Patch SM (STM32 DFU) is typically 0483:df11.
DFU_VIDPID ?= 0483:df11
DFU_SERIAL ?=
DFU_ALT    ?= 0
# Follows APP_TYPE via libDaisy: internal flash (0x08000000) for BOOT_NONE,
# QSPI (0x90040000) for BOOT_SRAM/BOOT_QSPI. BOOT_NONE here, so this writes
# straight to internal flash and needs no bootloader on the unit.
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

program-dfu-local: $(BUILD_DIR)/$(TARGET).bin
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
	out=`$(DFU_UTIL) -d $(DFU_VIDPID) -S $$serial_to_use -a $(DFU_ALT) -s $(DFU_ADDR) -D $(BUILD_DIR)/$(TARGET).bin 2>&1` || status=$$?; \
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
