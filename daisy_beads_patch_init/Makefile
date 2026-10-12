# daisy_beads_patch_init — Wish on Daisy patch.Init(), for a stock Electrosmith Daisy
# patch.init(), based on Mutable Instruments Beads (Emilie Gillet, MIT)
#
#   make                build firmware -> build/wish_patch_init.bin
#   make program-dfu    flash it through the Daisy bootloader's DFU (BOOT_SRAM)
#
# The same Beads DSP as daisy_beads/ (Wish, for the Alchemy Lab), compiled the
# same way: from deps/mutable/eurorack/beads, with daisy_beads/src/upstream/
# searched first, so its two headers shadow Beads' own (see daisy_beads/Makefile),
# and daisy_beads/src/pot_laws.h for the pot laws. This Makefile builds against
# the repo's libDaisy (deps/daisy), as the other patch.init apps do.

TARGET = wish_patch_init

PROJECT_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
_P_DIR := $(patsubst %/,%,$(dir $(PROJECT_MAKEFILE)))
_P_UP1 := $(abspath $(_P_DIR)/..)

include $(_P_UP1)/make/common.mk

ifeq ($(wildcard $(MUTABLE_EURORACK)/beads/dsp/processor.cc),)
$(error $(MUTABLE_EURORACK)/beads is missing. Beads needs pichenettes/eurorack d8f4739 or later: git submodule update --init deps/mutable/eurorack)
endif

# No DaisySP here, and libDaisy's core Makefile links -ldaisysp whenever
# DAISYSP_DIR is set -- which common.mk always does. Emptied, as daisy_beads.
DAISYSP_DIR :=

WISH_DIR   := $(_P_UP1)/daisy_beads
BEADS_DIR  := $(MUTABLE_EURORACK)/beads
STMLIB_DIR := $(MUTABLE_EURORACK)/stmlib

BUILD_DIR ?= build

CPP_SOURCES = src/main.cpp

# Beads: the portable part, the same list as daisy_beads/Makefile.
CC_SOURCES = \
    $(BEADS_DIR)/dsp/processor.cc \
    $(BEADS_DIR)/dsp/synth.cc \
    $(BEADS_DIR)/dsp/delay_clock.cc \
    $(BEADS_DIR)/dsp/delay_controller.cc \
    $(BEADS_DIR)/dsp/mu_law.cc \
    $(BEADS_DIR)/dsp/trigger_generator.cc \
    $(BEADS_DIR)/gain_control.cc \
    $(BEADS_DIR)/resources.cc \
    $(STMLIB_DIR)/dsp/units.cc \
    $(STMLIB_DIR)/utils/random.cc

# daisy_beads/src/upstream first: its two headers shadow Beads' own. common/
# holds voct_cal.h, Joy's V/OCT calibration.
C_INCLUDES += \
    -I$(WISH_DIR)/src \
    -I$(WISH_DIR)/src/upstream \
    -I$(_P_UP1)/common \
    -I$(MUTABLE_EURORACK)

# BOOT_SRAM: the code runs from AXI SRAM, loaded by the Daisy bootloader from
# QSPI, which leaves QSPI writable for the saved settings. It is ~400 KB, too
# big for the 128 KB internal flash.
APP_TYPE ?= BOOT_SRAM
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
ifeq ($(APP_TYPE),BOOT_QSPI)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_qspi.lds
else ifeq ($(APP_TYPE),BOOT_SRAM)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_sram.lds
else
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_flash.lds
endif
include $(SYSTEM_FILES_DIR)/Makefile

# stmlib's makefile.inc builds Mutable's code with these two off; its
# STATIC_ASSERT and type-punning trip them on every file otherwise. After the
# include, which sets CPPFLAGS outright.
CPPFLAGS += -Wno-unused-local-typedefs -Wno-strict-aliasing

# libDaisy's core Makefile compiles .cpp only; Mutable's sources are .cc.
CC_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
OBJECTS += $(CC_OBJECTS)
vpath %.cc $(sort $(dir $(CC_SOURCES)))

$(BUILD_DIR)/$(TARGET).elf: $(CC_OBJECTS)

$(BUILD_DIR)/%.o: %.cc Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cc=.lst)) $< -o $@
