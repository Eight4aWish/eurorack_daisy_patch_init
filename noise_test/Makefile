# Noise test firmware (temporary). Three builds from one source:
#   make BOARD=patchsm           -> build/patchsm/noise_test_patchsm.bin       stock patch.init
#   make BOARD=patchsm OLED=1    -> build/patchsm_oled/noise_test_patchsm_oled.bin
#   make BOARD=seed              -> build/seed/noise_test_seed.bin             hand-built Seed module
# ./build_all.sh makes all three.
BOARD ?= patchsm
OLED  ?= 0
VARIANT := $(BOARD)$(if $(filter 1,$(OLED)),_oled,)
TARGET   = noise_test_$(VARIANT)

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

BUILD_DIR = build/$(VARIANT)

CPP_SOURCES = src/main.cpp
C_INCLUDES += -I$(_P_UP1)/common
ifeq ($(BOARD),patchsm)
C_DEFS += -DBOARD_PATCHSM -DOLED=$(OLED)
ifeq ($(OLED),1)
CPP_SOURCES += $(_P_UP1)/common/oled_soft_i2c.cpp
endif
else ifeq ($(BOARD),seed)
C_DEFS += -DBOARD_SEED -DOLED=1
else
$(error BOARD must be patchsm or seed)
endif

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
APP_TYPE ?= BOOT_NONE
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_flash.lds
include $(SYSTEM_FILES_DIR)/Makefile
