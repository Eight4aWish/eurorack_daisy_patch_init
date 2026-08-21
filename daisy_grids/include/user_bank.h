// User drum-map bank, loaded from the SD card at boot.
//
// The repo ships the tool for deriving banks, not every bank worth having - see
// tools/groove_nodes/. That matters because the best corpus for a given person
// is usually one they already own and cannot redistribute: a commercial MIDI
// library, their own sequencer's patterns. Deriving a bank from those locally is
// fine; shipping it is not.
//
// Keeping it on the card rather than compiling it in would mean one firmware
// binary for everyone: three factory banks, plus a fourth if a bank file is on
// the card, swappable without a rebuild.
//
// PARKED. It does not work, and the build defaults to SORROW_SD_USER_BANK=0.
// What is known, so the next attempt does not start from nothing:
//
//   - SdmmcHandler::Init returns OK, FatFSInterface::Init returns OK, and
//     GetSDPath() gives a correct "0:/". So the peripheral comes up and the
//     driver is linked.
//   - f_mount with opt=1 returns FR_DISK_ERR after 30,025 ms - which is
//     sd_diskio.c's SD_TIMEOUT of 30 s almost exactly. It is spinning on
//     `while(ReadStatus == 0 && ...)`, so the transfer never completes.
//   - SDMMC1_IRQHandler is linked (not the weak default), NVIC is enabled at
//     priority 0, and sd_diskio.o is in the link, so the strong
//     BSP_SD_ReadCpltCallback that sets ReadStatus should win over the weak
//     stub in bsp_sd_diskio.c.
//   - Moving the FATFS object, the FIL and the destination buffer into
//     DMA_BUFFER_MEM_SECTION (.sram1_bss at 0x30000000) did not help, though it
//     was necessary anyway: the SDMMC IDMA cannot reach DTCMRAM, where .bss
//     lands by default in a BOOT_SRAM build.
//   - The bootloader reads the same card successfully to flash the firmware, so
//     card, wiring and filesystem are all fine. It is the app's SD stack.
//
// Untried: whether the bootloader leaves the peripheral in a state a plain
// re-init does not clear, and whether a full SDMMC de-init first would fix it.
// The sample player will need this path working, so it is worth returning to.
//
// Format: kUserBankBytes of raw uint8_t - 25 nodes of 96, exactly as the
// compiled-in banks are laid out, row-major across the 5x5 map. Generate one
// with tools/groove_nodes/som.py, which writes it alongside the C++ tables.
//
// The extension is deliberately not .bin. The Daisy bootloader scans the card
// root for the first .bin and flashes it to QSPI as firmware, so a 2,400-byte
// bank file sitting there under that name is a loaded gun: pick it up and the
// module is bricked until it is reflashed.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace sorrow
{

// 25 nodes * 3 instruments * 32 steps
constexpr uint32_t kUserBankBytes = 25 * 96;

// Filename looked for in the card root.
constexpr const char* kUserBankFile = "sorrow_bank.dat";

// Mount the card and read the bank if it is there. Returns true if a bank was
// loaded, in which case the pattern generator has a fourth bank. Safe to call
// with no card present, and safe to ignore the result.
bool LoadUserBankFromSd();

// Which stage the last load attempt reached, for the boot log: "loaded",
// "no bank file", "sd init", "mount", "short read"...
const char* UserBankStatus();

// Raw result codes and the drive path from the last attempt. Stage names say
// where it stopped; these say why, which is the difference between guessing and
// knowing when the failure is inside somebody else's driver.
int         UserBankSdResult();
int         UserBankFsResult();
int         UserBankMountResult();
int         UserBankOpenResult();
const char* UserBankPath();

} // namespace sorrow
