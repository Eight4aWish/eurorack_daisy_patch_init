# Project Name
TARGET = daisy_multifx_oled

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
C_INCLUDES += -I$(DAISYSP_DIR)/DaisySP-LGPL/Source

# Link DaisySP-LGPL for ReverbSc
LIBS += -ldaisysp-lgpl
LIBDIR += -L$(DAISYSP_DIR)/DaisySP-LGPL/build

# Linker configuration
LDFLAGS += -u _printf_float

# Core location and generic makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# App type / linker selection (flash for standalone, no bootloader)
APP_TYPE ?= BOOT_NONE

ifeq ($(APP_TYPE),BOOT_QSPI)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_qspi.lds
else ifeq ($(APP_TYPE),BOOT_SRAM)
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_sram.lds
else
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_flash.lds
endif
include $(SYSTEM_FILES_DIR)/Makefile
