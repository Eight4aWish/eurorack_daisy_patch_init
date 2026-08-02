# Project Name
TARGET = joy_lite

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

# Full Braids library – all oscillator models (Joy Lite exposes a curated 16)
C_DEFS += -DBRAIDS_VARIANT_FULL

# Shared Mutable Instruments
BRAIDS_DIR ?= $(MUTABLE_EURORACK)/braids
STMLIB_DIR ?= $(MUTABLE_EURORACK)/stmlib

# Build directory
BUILD_DIR ?= build

# Sources (screenless: no OLED driver)
CPP_SOURCES = src/main.cpp

# Mutable Instruments Braids sources (.cc files)
CC_SOURCES = $(BRAIDS_DIR)/macro_oscillator.cc \
             $(BRAIDS_DIR)/analog_oscillator.cc \
             $(BRAIDS_DIR)/digital_oscillator.cc \
             $(BRAIDS_DIR)/resources.cc \
             $(STMLIB_DIR)/dsp/units.cc \
             $(STMLIB_DIR)/utils/random.cc

# Additional include paths (common/ holds voct_cal.h + joy_dsp.h)
C_INCLUDES += -I$(_P_UP1)/common
C_INCLUDES += -I$(MUTABLE_EURORACK)

# Linker configuration
LDFLAGS += -u _printf_float

# Core location and generic makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# BOOT_SRAM: runs from SRAM (loaded via the Daisy bootloader from QSPI), leaving
# QSPI writable at runtime for persistent settings (calibration + last model).
APP_TYPE ?= BOOT_SRAM

ifeq ($(APP_TYPE),BOOT_QSPI)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_qspi.lds
else ifeq ($(APP_TYPE),BOOT_SRAM)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_sram.lds
else
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_flash.lds
endif
include $(SYSTEM_FILES_DIR)/Makefile

#######################################
# Extensions to libDaisy core Makefile
#######################################

CC_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
OBJECTS += $(CC_OBJECTS)
vpath %.cc $(sort $(dir $(CC_SOURCES)))

$(BUILD_DIR)/$(TARGET).elf: $(CC_OBJECTS)

$(BUILD_DIR)/%.o: %.cc Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cc=.lst)) $< -o $@
