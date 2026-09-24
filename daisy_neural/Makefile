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
