# Project Name
TARGET = oled_test

# Sources
CPP_SOURCES = oled_test.cpp

# Library Locations
LIBDAISY_DIR = ../deps/daisy/libDaisy
DAISYSP_DIR = ../deps/daisy/DaisySP

# Linker flags (use SRAM for faster execution)
LDFLAGS += -u _printf_float

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
