# IntervalOsc - Dual oscillator with interval offset for Daisy Patch.Init()
# Ported from https://github.com/ndonald2/DaisyPatches
TARGET = IntervalOsc

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

# Sources
CPP_SOURCES = IntervalOsc.cpp

# Board selection (enables proper MCU includes/defines in libDaisy core Makefile)
USE_DAISY_PATCH_SM = 1
USE_DAISYSP_LGPL = 1

DAISYSP_LGPL_DIR ?= $(DAISYSP_DIR)/DaisySP-LGPL

C_INCLUDES += \
-I. \
-I$(LIBDAISY_DIR)/src \
-I$(DAISYSP_DIR)/Source \
-I$(DAISYSP_LGPL_DIR)/Source

OPT = -O2

LIBDAISY_CORE_MAKE := $(LIBDAISY_DIR)/core/Makefile

ifeq ($(wildcard $(LIBDAISY_CORE_MAKE)),)
$(error libDaisy core Makefile not found at $(LIBDAISY_CORE_MAKE). Set DAISY_ROOT (parent containing libDaisy/ and DaisySP/), or set LIBDAISY_DIR/DAISYSP_DIR explicitly.)
endif

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(LIBDAISY_CORE_MAKE)

# Ensure required static libraries exist (fresh-clone convenience)
LIBDAISY_LIB ?= $(LIBDAISY_DIR)/build/libdaisy.a
DAISYSP_LIB  ?= $(DAISYSP_DIR)/build/libdaisysp.a

$(LIBDAISY_LIB):
	$(MAKE) -C $(LIBDAISY_DIR)

$(DAISYSP_LIB):
	$(MAKE) -C $(DAISYSP_DIR)

$(BUILD_DIR)/$(TARGET).elf: $(LIBDAISY_LIB) $(DAISYSP_LIB)

# Flash target using dfu-util
program-dfu: build/$(TARGET).bin
	@status=0; \
	dfu-util -a 0 -s 0x08000000:leave -D build/$(TARGET).bin || status=$$?; \
	if [ $$status -eq 74 ]; then \
		echo "dfu-util returned 74 on :leave; firmware was written OK. Tap RESET."; \
		exit 0; \
	fi; \
	exit $$status

flash: program-dfu
