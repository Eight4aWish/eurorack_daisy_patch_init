// Loads a user drum-map bank from the SD card - see user_bank.h.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 David Baghurst

#include "user_bank.h"

#include "grids_port.h"

#include "daisy_core.h"
#include "daisy_patch_sm.h"
#include "fatfs.h"

#include <cstdio>
#include <cstring>

using namespace daisy;

namespace sorrow
{
namespace
{
// All three of these have to sit in DMA-reachable memory.
//
// SDMMC moves data by IDMA, and on the H7 the IDMA cannot see DTCMRAM - which is
// where .bss and the stack land by default in this build. A transfer targeting
// DTCM never completes, the driver's completion callback never fires, and
// sd_diskio.c spins on `while(ReadStatus == 0 && ...)` for its full SD_TIMEOUT
// of 30 seconds before giving up. Measured on hardware: mount failing at
// 30,025 ms, which is that constant almost exactly.
//
// So the FatFS object (which owns the sector window buffer), the file handle
// (which owns its own), and the destination all go to .sram1_bss.

// The bank lives here for the life of the firmware. Static rather than heap:
// there is no allocator worth using on this target and the size is fixed.
uint8_t DMA_BUFFER_MEM_SECTION s_bank[kUserBankBytes];

SdmmcHandler                  s_sd;
FatFSInterface DMA_BUFFER_MEM_SECTION s_fsi;
FIL            DMA_BUFFER_MEM_SECTION s_file;
} // namespace

const char* g_last_stage = "not attempted";
int  g_sd_result    = -1;
int  g_fs_result    = -1;
int  g_mount_result = -1;
int  g_open_result  = -1;
char g_path[64]     = "";

const char* UserBankStatus()
{
    return g_last_stage;
}

int         UserBankSdResult() { return g_sd_result; }
int         UserBankFsResult() { return g_fs_result; }
int         UserBankMountResult() { return g_mount_result; }
int         UserBankOpenResult() { return g_open_result; }
const char* UserBankPath() { return g_path; }

bool LoadUserBankFromSd()
{
    SdmmcHandler::Config cfg;
    cfg.Defaults();
    // STANDARD rather than the FAST default. Init is always negotiated at
    // 400 kHz and only then raised, and pushing a hand-wired card slot to 50 MHz
    // is where it stalls - which shows up as a long pause at boot followed by no
    // bank, because the retries eat seconds and then fail anyway. 25 MHz is far
    // more than a 2,400-byte read needs.
    cfg.speed = SdmmcHandler::Speed::STANDARD;

    // The bootloader has just finished using this card - it reads the firmware
    // off it - and hands over with the peripheral still configured. Give the
    // card a moment to settle before claiming it again.
    System::Delay(250);

    g_last_stage = "sd init";
    g_sd_result  = static_cast<int>(s_sd.Init(cfg));
    if(g_sd_result != static_cast<int>(SdmmcHandler::Result::OK))
        return false;

    g_last_stage = "fatfs link";
    g_fs_result  = static_cast<int>(s_fsi.Init(FatFSInterface::Config::MEDIA_SD));
    if(g_fs_result != static_cast<int>(FatFSInterface::Result::OK))
        return false;

    g_last_stage = "mount";
    std::snprintf(g_path, sizeof g_path, "[%s]", s_fsi.GetSDPath());
    // Deferred mount (opt=0): no I/O happens here, so a card problem cannot
    // cost SD_TIMEOUT's thirty seconds at this point. The filesystem is read on
    // first access instead, which is f_open below.
    g_mount_result = static_cast<int>(
        f_mount(&s_fsi.GetSDFileSystem(), s_fsi.GetSDPath(), 0));
    if(g_mount_result != FR_OK)
        return false;

    // Fully qualified path. A bare filename relies on the SD volume being the
    // current drive, which is not guaranteed once more than one is registered.
    char path[64];
    std::snprintf(path, sizeof path, "%s%s", s_fsi.GetSDPath(), kUserBankFile);

    g_last_stage  = "open";
    g_open_result = static_cast<int>(f_open(&s_file, path, FA_READ));
    if(g_open_result != FR_OK)
    {
        g_last_stage = "no bank file";
        return false;   // the normal case on a card without one
    }

    UINT read = 0;
    const FRESULT r = f_read(&s_file, s_bank, kUserBankBytes, &read);
    f_close(&s_file);

    // A short or unreadable file is rejected outright rather than padded: a
    // half-loaded bank would play as silence in the missing nodes and look like
    // a firmware bug rather than a bad file.
    if(r != FR_OK || read != kUserBankBytes)
    {
        g_last_stage = "short read";
        return false;
    }

    g_last_stage = "loaded";
    daisy_grids::grids_port::SetUserBank(s_bank);
    return true;
}

} // namespace sorrow
