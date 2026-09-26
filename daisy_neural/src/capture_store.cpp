#include "capture_store.h"

#include "fatfs.h"
#include <cstring>

using namespace daisy;

namespace captures
{
namespace
{

// The SD peripheral and FatFS state. DMA_BUFFER_MEM_SECTION for the same reason
// daisy_grids uses it: these are touched by DMA and must not sit in DTCMRAM,
// which the D-cache and the SDMMC controller do not agree about.
SdmmcHandler                  s_sd;
FatFSInterface DMA_BUFFER_MEM_SECTION s_fsi;
FIL            DMA_BUFFER_MEM_SECTION s_file;

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
    if(s_sd.Init(cfg) != SdmmcHandler::Result::OK)
        return false;

    s_status = "fs init";
    s_fsi.Init(FatFSInterface::Config::MEDIA_SD);

    s_status = "mount";
    if(f_mount(&s_fsi.GetSDFileSystem(), s_fsi.GetSDPath(), 1) != FR_OK)
        return false;

    s_status = "scan";
    DIR     dir;
    FILINFO info;
    if(f_opendir(&dir, s_fsi.GetSDPath()) != FR_OK)
        return false;

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
    if((size_t)e->weight_count > dst_capacity)
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

    // Read straight into dst, then check the CRC. If it fails the caller is
    // told and must not use dst — which is why the engine is only handed these
    // weights after this returns true.
    got = 0;
    const bool read_ok
        = (f_read(&s_file, dst, (UINT)bytes, &got) == FR_OK) && (got == bytes);
    f_close(&s_file);

    if(!read_ok)
    {
        s_status = "read fail";
        return false;
    }

    if(Crc32((const uint8_t*)dst, bytes) != want_crc)
    {
        s_status = "bad crc";
        return false;
    }

    s_status = "ok";
    return true;
}

} // namespace captures
