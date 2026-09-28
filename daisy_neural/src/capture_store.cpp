#include "capture_store.h"

#include "fatfs.h"
#include <cstring>

using namespace daisy;

// libDaisy's HAL handle for SDMMC1, a plain global in per/sdmmc.cpp.
extern SD_HandleTypeDef hsd1;

namespace captures
{
namespace
{

// Where the card's DMA lands. SDRAM, which is the configuration that works on
// hardware (2026-09-28). The SDMMC1 DMA cannot reach DTCMRAM, where BOOT_SRAM
// puts ordinary .bss. These were moved here from D2 (DMA_BUFFER_MEM_SECTION)
// while chasing a mount failure that turned out to be the card's partition size
// (see Init), so whether D2 would also have worked is untested; SDRAM is kept
// because it is what has been seen to work. libDaisy's own SD example is
// BOOT_NONE, whose .bss is AXI SRAM. sd_diskio.c does the cache maintenance
// that cacheable SDRAM needs.
//
// FatFS keeps its sector window inside the FATFS object and each FIL has its
// own sector buffer (_FS_TINY 0), so both objects must be reachable.
SdmmcHandler                s_sd;
FatFSInterface DSY_SDRAM_BSS s_fsi;
FIL DSY_SDRAM_BSS            s_file;

// Whole-sector reads bypass FIL's buffer and DMA straight into the caller's
// destination, and the engine's weight buffer is in DTCMRAM. So weights come
// through here first. 8 KB covers A2-Lite's 7,484 bytes.
constexpr size_t kBounceBytes = 8192;
alignas(32) uint8_t DSY_SDRAM_BSS s_bounce[kBounceBytes];

// Status text with FatFS's own result code appended, e.g. "mount 1". The stage
// says where it stopped; the code says why.
char s_status_buf[12];
const char* Fail(const char* stage, int code)
{
    snprintf(s_status_buf, sizeof(s_status_buf), "%s %d", stage, code);
    return s_status_buf;
}
// Same, for the HAL's error bitfield (HAL_SD_ERROR_* in stm32h7xx_hal_sd.h).
const char* FailHex(const char* stage, uint32_t bits)
{
    snprintf(s_status_buf, sizeof(s_status_buf), "%s %lx", stage, (unsigned long)bits);
    return s_status_buf;
}

Entry       s_entries[kMaxFiles];
int         s_count  = 0;
bool        s_ready  = false;
const char* s_status = "not started";

// CRC32, IEEE 802.3 — same polynomial as zlib, which is what the exporter uses.
uint32_t Crc32(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for(size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for(int b = 0; b < 8; b++)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t Rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                                       | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t Rd16(const uint8_t* p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

bool EndsWithA2nb(const char* name)
{
    const size_t n = strlen(name);
    if(n < 5)
        return false;
    const char* e = name + n - 5;
    return (e[0] == '.')
           && (e[1] == 'a' || e[1] == 'A')
           && (e[2] == '2')
           && (e[3] == 'n' || e[3] == 'N')
           && (e[4] == 'b' || e[4] == 'B');
}

/** Read and validate a header. Fills entry on success. */
bool ReadHeader(const char* path, Entry& entry)
{
    if(f_open(&s_file, path, FA_READ) != FR_OK)
        return false;

    uint8_t hdr[kHeaderBytes];
    UINT    got = 0;
    const bool ok = (f_read(&s_file, hdr, sizeof(hdr), &got) == FR_OK) && (got == sizeof(hdr));
    f_close(&s_file);
    if(!ok)
        return false;

    if(Rd32(hdr + 0) != kMagic || Rd16(hdr + 4) != kVersion)
        return false;

    entry.weight_count = (int)Rd32(hdr + 8);
    if(entry.weight_count <= 0 || entry.weight_count > 65536)
        return false;

    uint32_t gain_bits = Rd32(hdr + 12);
    memcpy(&entry.gain, &gain_bits, sizeof(float));

    memset(entry.name, 0, sizeof(entry.name));
    memcpy(entry.name, hdr + 16, kNameLen);
    entry.name[kNameLen] = '\0';

    snprintf(entry.path, kPathLen, "%s", path);
    return true;
}

} // namespace

const char* Status() { return s_status; }
int         Count() { return s_count; }

const Entry* Get(int index)
{
    if(index < 0 || index >= s_count)
        return nullptr;
    return &s_entries[index];
}

bool Init()
{
    s_count = 0;
    s_ready = false;

    SdmmcHandler::Config cfg;
    cfg.Defaults();
    // STANDARD rather than the FAST default, following daisy_grids: init is
    // negotiated at 400 kHz and only then raised, and pushing a card slot to
    // 50 MHz is where it stalls — which presents as a long pause at boot and
    // then no captures, because the retries eat seconds and fail anyway.
    // 25 MHz is far more than a 7.5 KB read needs.
    cfg.speed = SdmmcHandler::Speed::STANDARD;

    // Under BOOT_SRAM the bootloader has just used this card to read the
    // firmware and hands over with the peripheral still configured. Let it
    // settle before claiming it.
    System::Delay(250);

    s_status = "sd init";
    const auto sd = s_sd.Init(cfg);
    if(sd != SdmmcHandler::Result::OK)
    {
        s_status = Fail("sd init", (int)sd);
        return false;
    }

    s_status = "fs init";
    s_fsi.Init(FatFSInterface::Config::MEDIA_SD);

    // Run BSP_SD_Init()'s four steps here, so a card the driver refuses shows
    // which step and the HAL's error bits instead of a bare FR_NOT_READY from
    // f_mount. f_mount repeats them afterwards, which is harmless.
    //
    // THE CARD MUST HAVE A FAT32 PARTITION OF 2 GB OR LESS. Found 2026-09-28: a
    // 64 GB card formatted as one full-size FAT32 volume failed right here with
    // "hal 100000" (WP_ERASE_SKIP, a status that makes no sense during
    // identification). The same card repartitioned to a single 2 GB FAT32
    // volume, rest unallocated, loaded all five captures. Why is not known;
    // the Daisy bootloader also reads the card at every boot, which is one
    // place the volume size could matter before the app ever sees it.
    s_status = "hal";
    if(HAL_SD_Init(&hsd1) != HAL_OK)
    {
        s_status = FailHex("hal", hsd1.ErrorCode);
        return false;
    }
    if(HAL_SD_ConfigWideBusOperation(&hsd1, hsd1.Init.BusWide) != HAL_OK)
    {
        s_status = FailHex("wide", hsd1.ErrorCode);
        return false;
    }
    if(HAL_SD_ConfigSpeedBusOperation(&hsd1, SDMMC_SPEED_MODE_AUTO) != HAL_OK)
    {
        s_status = FailHex("spd", hsd1.ErrorCode);
        return false;
    }
    const HAL_SD_CardStateTypeDef cs = HAL_SD_GetCardState(&hsd1);
    if(cs != HAL_SD_CARD_TRANSFER)
    {
        s_status = Fail("state", (int)cs);
        return false;
    }

    s_status = "mount";
    const FRESULT mr = f_mount(&s_fsi.GetSDFileSystem(), s_fsi.GetSDPath(), 1);
    if(mr != FR_OK)
    {
        s_status = Fail("mount", (int)mr);
        return false;
    }

    s_status = "scan";
    DIR     dir;
    FILINFO info;
    const FRESULT dr = f_opendir(&dir, s_fsi.GetSDPath());
    if(dr != FR_OK)
    {
        s_status = Fail("scan", (int)dr);
        return false;
    }

    while(s_count < kMaxFiles)
    {
        if(f_readdir(&dir, &info) != FR_OK || info.fname[0] == 0)
            break;
        if(info.fattrib & AM_DIR)
            continue;
        if(!EndsWithA2nb(info.fname))
            continue;

        char path[kPathLen];
        snprintf(path, sizeof(path), "%s%s", s_fsi.GetSDPath(), info.fname);

        Entry e;
        if(ReadHeader(path, e))
            s_entries[s_count++] = e;
    }
    f_closedir(&dir);

    // f_readdir order is whatever the filesystem gives, so sort by filename.
    // The exporter numbers them 0_..4_ precisely so this restores the order of
    // the table they came from. Insertion sort; at most eight entries.
    for(int i = 1; i < s_count; i++)
    {
        Entry key = s_entries[i];
        int   j   = i - 1;
        while(j >= 0 && strcmp(s_entries[j].path, key.path) > 0)
        {
            s_entries[j + 1] = s_entries[j];
            j--;
        }
        s_entries[j + 1] = key;
    }

    s_ready  = s_count > 0;
    s_status = s_ready ? "ok" : "no captures";
    return s_ready;
}

bool Load(int index, float* dst, size_t dst_capacity)
{
    const Entry* e = Get(index);
    if(!e || !dst)
        return false;
    if((size_t)e->weight_count > dst_capacity
       || (size_t)e->weight_count * sizeof(float) > kBounceBytes)
    {
        s_status = "too big";
        return false;
    }

    if(f_open(&s_file, e->path, FA_READ) != FR_OK)
    {
        s_status = "open fail";
        return false;
    }

    uint8_t hdr[kHeaderBytes];
    UINT    got = 0;
    if(f_read(&s_file, hdr, sizeof(hdr), &got) != FR_OK || got != sizeof(hdr))
    {
        f_close(&s_file);
        s_status = "hdr read";
        return false;
    }

    const uint32_t want_crc = Rd32(hdr + 28);
    const size_t   bytes    = (size_t)e->weight_count * sizeof(float);

    // Read into the SDRAM bounce buffer (see kBounceBytes), check the CRC, and
    // only then copy into dst — so a failed load leaves dst untouched.
    got = 0;
    const FRESULT rr = f_read(&s_file, s_bounce, (UINT)bytes, &got);
    f_close(&s_file);

    if(rr != FR_OK || got != bytes)
    {
        s_status = Fail("read", (int)rr);
        return false;
    }

    if(Crc32(s_bounce, bytes) != want_crc)
    {
        s_status = "bad crc";
        return false;
    }

    memcpy(dst, s_bounce, bytes);
    s_status = "ok";
    return true;
}

} // namespace captures
