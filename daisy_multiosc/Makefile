# daisy_multiosc — boot-selectable multi-voice firmware (Daisy Patch Init)
TARGET = daisy_multiosc

# Library locations (override via environment or command line)
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

# Board selection
USE_DAISY_PATCH_SM = 1

BUILD_DIR ?= build

# Sources: app + shared host + OLED driver + engines (each in its own folder)
CPP_SOURCES = main.cpp \
              $(_P_UP1)/common/multiosc_core/host.cpp \
              $(_P_UP1)/common/oled_soft_i2c.cpp \
              $(_P_UP1)/daisy_fm4op/fm4op_voice.cpp

C_INCLUDES += \
-I. \
-I$(_P_UP1)/common \
-I$(_P_UP1)/daisy_fm4op \
-I$(LIBDAISY_DIR)/src \
-I$(DAISYSP_DIR)/Source

OPT = -O2

# Core location and generic makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# App type / linker selection (QSPI for bootloader support)
APP_TYPE ?= BOOT_QSPI

ifeq ($(APP_TYPE),BOOT_QSPI)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_qspi.lds
else ifeq ($(APP_TYPE),BOOT_SRAM)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_sram.lds
else
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_flash.lds
endif
include $(SYSTEM_FILES_DIR)/Makefile

# Ensure required static libraries exist (fresh-clone convenience)
LIBDAISY_LIB ?= $(LIBDAISY_DIR)/build/libdaisy.a
DAISYSP_LIB  ?= $(DAISYSP_DIR)/build/libdaisysp.a

$(LIBDAISY_LIB):
	$(MAKE) -C $(LIBDAISY_DIR)

$(DAISYSP_LIB):
	$(MAKE) -C $(DAISYSP_DIR)

$(BUILD_DIR)/$(TARGET).elf: $(LIBDAISY_LIB) $(DAISYSP_LIB)

#######################################
# SD-card flashing (macOS-safe)
#######################################
# Copy the BOOT_QSPI binary onto a mounted SD card for the Daisy bootloader.
# Wipes stale .bin first (the bootloader grabs the first .bin and stops),
# strips macOS AppleDouble "._" sidecars (they also end in .bin and confuse the
# scanner), and flushes. Then eject and power-cycle the Daisy.
#
#   make program-sd SD=/Volumes/DAISY
SD ?=
program-sd: $(BUILD_DIR)/$(TARGET).bin
	@if [ -z "$(SD)" ]; then echo "Set SD=/Volumes/<card>  (e.g. make program-sd SD=/Volumes/DAISY)"; exit 1; fi
	@if [ ! -d "$(SD)" ]; then echo "SD path '$(SD)' not found/mounted"; exit 1; fi
	@echo "Clearing stale binaries on $(SD)..."
	rm -f "$(SD)"/*.bin "$(SD)"/._*.bin
	cp "$(BUILD_DIR)/$(TARGET).bin" "$(SD)"/
	@dot_clean "$(SD)" 2>/dev/null || rm -f "$(SD)"/._*
	sync
	@echo "Copied $(TARGET).bin to $(SD). Eject the card, then power-cycle the Daisy."
