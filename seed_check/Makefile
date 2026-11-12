# seed_check: a bench check for a Daisy Seed taken off a board. See README.md.
#   make             -> build/seed_check.bin (internal flash, BOOT_NONE)
#   make program-dfu    the Seed in its ROM bootloader: hold BOOT, tap RESET
TARGET = seed_check

PROJECT_MAKEFILE := $(abspath $(lastword $(MAKEFILE_LIST)))
_P_DIR := $(patsubst %/,%,$(dir $(PROJECT_MAKEFILE)))
_P_UP1 := $(abspath $(_P_DIR)/..)
include $(_P_UP1)/make/common.mk

LIBDAISY_DIR ?= $(DAISY_ROOT)/libDaisy
BUILD_DIR = build
CPP_SOURCES = src/main.cpp

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
APP_TYPE ?= BOOT_NONE
LDSCRIPT ?= $(SYSTEM_FILES_DIR)/STM32H750IB_flash.lds
include $(SYSTEM_FILES_DIR)/Makefile
