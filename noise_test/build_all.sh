#!/bin/bash
# Builds the three noise-test images, each in its own folder (libDaisy's Makefile resets
# BUILD_DIR, so it goes on the command line). See README.md.
set -e
cd "$(dirname "$0")"
mkdir -p build/patchsm build/patchsm_oled build/seed  # its mkdir has no -p
make -s BOARD=patchsm        BUILD_DIR=build/patchsm
make -s BOARD=patchsm OLED=1 BUILD_DIR=build/patchsm_oled
make -s BOARD=seed           BUILD_DIR=build/seed
ls -l build/*/noise_test_*.bin
