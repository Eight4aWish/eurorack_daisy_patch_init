# Project Name
TARGET = braids_mi

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

LIBDAISY_LIB ?= $(LIBDAISY_DIR)/build/libdaisy.a

# Shared Mutable Instruments (preferred)
# (MUTABLE_EURORACK default comes from make/common.mk)
BRAIDS_DIR ?= $(MUTABLE_EURORACK)/braids
STMLIB_DIR ?= $(MUTABLE_EURORACK)/stmlib

# Sources
CPP_SOURCES = \
	src/main.cpp \

# Build directory (used for generated config headers)
BUILD_DIR ?= build

# Braids build variant:
# - lite: trimmed analog-only subset (fits internal flash)
# - full: upstream Braids macro oscillator + digital oscillator (recommended for BOOT_QSPI)
BRAIDS_VARIANT ?= full

# V/Oct tuning defaults (can be overridden on the make command line).
# Note: we intentionally ignore any exported shell environment variables for
# these knobs; only explicit command-line overrides should win.
# Examples:
#  - 0V = C2: make ... VOCT_BASE_MIDI=36
#  - 0V = C3: make ... VOCT_BASE_MIDI=48
#  - Center trim: make ... VOCT_CENTER_NORM=0.497f
ifeq ($(origin VOCT_BASE_MIDI),command line)
else
VOCT_BASE_MIDI = 48
endif

ifeq ($(origin VOCT_CENTER_NORM),command line)
else
VOCT_CENTER_NORM = 0.5f
endif

# Auto-generate a config header so changes to VOCT_* force recompiles.
C_INCLUDES += -include $(BUILD_DIR)/voct_config.h

# Braids DSP is .cc
ifeq ($(BRAIDS_VARIANT),full)
C_DEFS += -DBRAIDS_VARIANT_FULL
CC_SOURCES = \
	$(BRAIDS_DIR)/macro_oscillator.cc \
	$(BRAIDS_DIR)/digital_oscillator.cc \
	$(BRAIDS_DIR)/analog_oscillator.cc \
	$(BRAIDS_DIR)/resources.cc \
	$(STMLIB_DIR)/dsp/units.cc \
	$(STMLIB_DIR)/utils/random.cc

# Important: do not include local src/braids overrides (they limit models).
C_INCLUDES += \
	-I. \
	-I$(MUTABLE_EURORACK)
else
CC_SOURCES = \
	src/braids/macro_oscillator_mini.cc \
	$(BRAIDS_DIR)/analog_oscillator.cc \
	$(BRAIDS_DIR)/resources.cc \
	$(STMLIB_DIR)/dsp/units.cc \
	$(STMLIB_DIR)/utils/random.cc

C_INCLUDES += \
	-I. \
	-Isrc \
	-I$(MUTABLE_EURORACK)
endif

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# App type / linker selection
# - BOOT_NONE: normal app in internal flash (DFU-friendly)
# - BOOT_QSPI: Daisy bootloader app stored in external QSPI (SD card can flash it)
# - BOOT_SRAM: Daisy bootloader app running from SRAM
APP_TYPE ?= BOOT_QSPI

ifeq ($(APP_TYPE),BOOT_QSPI)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_qspi.lds
else ifeq ($(APP_TYPE),BOOT_SRAM)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_sram.lds
else
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_flash.lds
endif
include $(SYSTEM_FILES_DIR)/Makefile

### Need to override all to get support for .cc files
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

#######################################
# build the application
#######################################
# list of objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(CPP_SOURCES:.cpp=.o)))
vpath %.cpp $(sort $(dir $(CPP_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
vpath %.cc $(sort $(dir $(CC_SOURCES)))
# list of ASM program objects
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile $(BUILD_DIR)/voct_config.h | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $(C_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.cpp Makefile $(BUILD_DIR)/voct_config.h | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cpp=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.cc Makefile $(BUILD_DIR)/voct_config.h | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cc=.lst)) $< -o $@

$(BUILD_DIR)/voct_config.h: Makefile | $(BUILD_DIR)
	@printf '%s\n' \
		'#pragma once' \
		'#define VOCT_BASE_MIDI $(VOCT_BASE_MIDI)' \
		'#define VOCT_CENTER_NORM $(VOCT_CENTER_NORM)' \
		> $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(LIBDAISY_LIB):
	$(MAKE) -C $(LIBDAISY_DIR)

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) $(LIBDAISY_LIB) Makefile
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir $@
