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
              $(_P_UP1)/common/oled_soft_i2c.cpp

# Additional include paths
C_INCLUDES += -I$(_P_UP1)/common
# Project root, so src/main.cpp can include the lifted engine as "nam/...".
C_INCLUDES += -I$(_P_DIR)

# Linker configuration
LDFLAGS += -u _printf_float

# Core location and generic makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# App type / linker selection.
#
# BOOT_NONE while the engine slot is a passthrough: the skeleton is small, it
# flashes straight over DFU, and it does not need the Daisy bootloader on the
# unit — which matters because this unit currently runs MultiFX (BOOT_NONE), so
# the bootloader may not be installed. Phase 0 bench check 2 can therefore be
# run today.
#
# Switch to BOOT_SRAM when the A2 engine lands (phase 1 step 3): the weights
# want DTCM and the history buffer wants D2 SRAM, which needs the bootloader.
#   make APP_TYPE=BOOT_SRAM
APP_TYPE ?= BOOT_NONE

ifeq ($(APP_TYPE),BOOT_QSPI)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_qspi.lds
else ifeq ($(APP_TYPE),BOOT_SRAM)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_sram.lds
else
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_flash.lds
endif
include $(SYSTEM_FILES_DIR)/Makefile

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
