#include "capture_store.h"

#include "fatfs.h"
#include "nam_import.h"
#include <cmath>
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
// through here first. 8 KB covers A2-Lite's 7,484 bytes; a .grnb is read whole, header
// and all, and a GRU-24 with three inputs is 8,516 bytes, so 16 KB.
constexpr size_t kBounceBytes = 16384;
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
FilterEntry s_filters[kMaxFilters];
int         s_nfilters = 0;
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

bool EndsWith(const char* name, const char* ext)  // ext lower case, with its dot
{
    const size_t n = strlen(name), k = strlen(ext);
    if(n < k)
        return false;
    for(size_t i = 0; i < k; i++)
    {
        char c = name[n - k + i];
        if(c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if(c != ext[i])
            return false;
    }
    return true;
}
bool EndsWithA2nb(const char* name) { return EndsWith(name, ".a2nb"); }

// A .grnb's network payload in bytes (tools/grnb.py weight_count, gru::WeightCount).
size_t GrnbPayload(int h, int i) { return 4u * (size_t)(3 * h * i + 3 * h * h + 6 * h + h + 1); }

/** A .grnb header: magic, version (1, or 2 with a layer), plausible sizes. The CRCs are
 *  checked at load. */
bool ReadFilterHeader(const char* path, FilterEntry& entry)
{
    if(f_open(&s_file, path, FA_READ) != FR_OK)
        return false;
    uint8_t    hdr[kGrnbHeaderBytes];
    UINT       got  = 0;
    const bool ok   = (f_read(&s_file, hdr, sizeof(hdr), &got) == FR_OK) && (got == sizeof(hdr));
    const FSIZE_t size = f_size(&s_file);
    f_close(&s_file);
    const uint16_t version = ok ? Rd16(hdr + 4) : 0;
    if(!ok || Rd32(hdr + 0) != kGrnbMagic || (version != 1 && version != 2))
        return false;
    entry.hidden = Rd16(hdr + 8);
    entry.inputs = Rd16(hdr + 10);
    if(entry.hidden <= 0 || entry.hidden > 64 || entry.inputs <= 0 || entry.inputs > 4)
        return false;
    entry.layer_offset = (uint32_t)(kGrnbHeaderBytes + GrnbPayload(entry.hidden, entry.inputs));
    entry.layer_bytes  = version == 2 ? Rd32(hdr + 52) : 0;
    entry.layer_crc    = version == 2 ? Rd32(hdr + 56) : 0;
    entry.ctl_bytes    = Rd16(hdr + 6);  // the controls section, after everything else
    entry.ctl_crc      = Rd32(hdr + 60);
    if(version == 2 && entry.layer_bytes == 0)
        return false;
    if((FSIZE_t)entry.layer_offset + entry.layer_bytes + entry.ctl_bytes != size)
        return false;
    uint32_t gain_bits = Rd32(hdr + 12);
    memcpy(&entry.gain, &gain_bits, sizeof(float));
    memset(entry.name, 0, sizeof(entry.name));
    memcpy(entry.name, hdr + 16, kNameLen);
    entry.name[kNameLen] = '\0';
    snprintf(entry.path, kPathLen, "%s", path);
    return true;
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

int Find(const char* name)
{
    if(!name)
        return -1;
    for(int i = 0; i < s_count; i++)
        if(strcmp(s_entries[i].name, name) == 0)
            return i;
    return -1;
}

// ---------------------------------------------------------------------------
// .nam import (nam_import.h)
// ---------------------------------------------------------------------------
namespace
{
constexpr int    kMaxNam   = 32;
constexpr size_t kNamPath  = 300;       // FatFS long names run to 255
constexpr size_t kNamBytes = 2u << 20;  // a downloaded A2 .nam is about 300 KB
char DSY_SDRAM_BSS              s_nam_list[kMaxNam][256];
alignas(32) char DSY_SDRAM_BSS  s_nam_text[kNamBytes];
float DSY_SDRAM_BSS             s_nam_weights[nam_import::kWeights];

bool Exists(const char* path)
{
    FILINFO info;
    return f_stat(path, &info) == FR_OK;
}

// Move a .nam out of the root into folder/, numbering it if a file of that name is
// already there. Nothing is deleted.
void Retire(const char* name, const char* folder)
{
    char from[kNamPath], to[kNamPath];
    snprintf(from, sizeof(from), "%s%s", s_fsi.GetSDPath(), name);
    snprintf(to, sizeof(to), "%s%s/%s", s_fsi.GetSDPath(), folder, name);
    for(int k = 2; Exists(to) && k < 100; k++)
        snprintf(to, sizeof(to), "%s%s/%d_%s", s_fsi.GetSDPath(), folder, k, name);
    f_rename(from, to);
}

void Refuse(ImportResult& r, const char* name, const char* why)
{
    r.refused++;
    snprintf(r.last_refused, sizeof(r.last_refused), "%s", name);
    r.last_why = why;
    Retire(name, "nam_refused");
}
} // namespace

ImportResult ImportNam(LevelFn level, ProgressFn progress)
{
    ImportResult r;
    // List first: the root changes as files are written and moved.
    int     n = 0;
    DIR     dir;
    FILINFO info;
    if(f_opendir(&dir, s_fsi.GetSDPath()) != FR_OK)
        return r;
    while(n < kMaxNam && f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
        if(!(info.fattrib & AM_DIR) && EndsWith(info.fname, ".nam") && info.fname[0] != '.')
            snprintf(s_nam_list[n++], sizeof(s_nam_list[0]), "%s", info.fname);
    f_closedir(&dir);
    if(n == 0)
        return r;
    char folder[kNamPath];
    for(const char* sub : {"nam_done", "nam_refused"})
    {
        snprintf(folder, sizeof(folder), "%s%s", s_fsi.GetSDPath(), sub);
        f_mkdir(folder);  // FR_EXIST when it is already there
    }

    for(int i = 0; i < n; i++)
    {
        const char* name = s_nam_list[i];
        if(progress)
            progress(i, n, name);
        char path[kNamPath];
        snprintf(path, sizeof(path), "%s%s", s_fsi.GetSDPath(), name);
        if(f_open(&s_file, path, FA_READ) != FR_OK)
        {
            Refuse(r, name, "open fail");
            continue;
        }
        const FSIZE_t size = f_size(&s_file);
        UINT          got  = 0;
        const bool    fits = size < kNamBytes;
        const FRESULT rr   = fits ? f_read(&s_file, s_nam_text, (UINT)size, &got) : FR_OK;
        f_close(&s_file);
        if(!fits || rr != FR_OK || got != size)
        {
            Refuse(r, name, fits ? "read fail" : "too big");
            continue;
        }
        s_nam_text[got] = '\0';

        size_t      count = 0;
        const char* why   = "";
        if(!nam_import::ExtractLite(s_nam_text, got, s_nam_weights, nam_import::kWeights, &count, &why))
        {
            Refuse(r, name, why);
            continue;
        }

        // The name nam_to_a2nb.py would give it, numbered if that is taken.
        char shown[nam_import::kNameLen], file[64], out[kNamPath];
        nam_import::DefaultName(name, shown);
        nam_import::FileName(shown, file, sizeof(file));
        snprintf(out, sizeof(out), "%s%s", s_fsi.GetSDPath(), file);
        for(int k = 2; Exists(out) && k < 100; k++)
        {
            char base[nam_import::kNameLen];
            snprintf(base, sizeof(base), "%.*s", k < 10 ? 9 : 8, shown);  // room for " 2" or " 12"
            for(size_t e = strlen(base); e > 0 && base[e - 1] == ' ';)
                base[--e] = '\0';
            snprintf(shown, sizeof(shown), "%s %d", base, k);
            nam_import::FileName(shown, file, sizeof(file));
            snprintf(out, sizeof(out), "%s%s", s_fsi.GetSDPath(), file);
        }

        const float db = level(s_nam_weights);
        if(!std::isfinite(db))
        {
            Refuse(r, name, "no level");
            continue;
        }
        nam_import::Pack(shown, nam_import::GainFor(db), s_nam_weights, s_bounce);
        UINT wrote = 0;
        if(f_open(&s_file, out, FA_WRITE | FA_CREATE_NEW) != FR_OK)
        {
            Refuse(r, name, "write fail");
            continue;
        }
        const FRESULT wr = f_write(&s_file, s_bounce, (UINT)nam_import::kFileBytes, &wrote);
        const FRESULT cr = f_close(&s_file);
        if(wr != FR_OK || cr != FR_OK || wrote != nam_import::kFileBytes)
        {
            f_unlink(out);  // our own half-written file, never the user's
            Refuse(r, name, "write fail");
            continue;
        }
        Retire(name, "nam_done");
        r.added++;
    }
    if(progress)
        progress(n, n, "");
    return r;
}

bool Init(LevelFn level, ProgressFn progress, ImportResult* imported)
{
    s_count    = 0;
    s_nfilters = 0;
    s_ready    = false;

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
    // volume, rest unallocated, loaded every capture. Why is not known;
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

    if(level)
    {
        s_status = "import";
        const ImportResult r = ImportNam(level, progress);
        if(imported)
            *imported = r;
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

    while(true)
    {
        if(f_readdir(&dir, &info) != FR_OK || info.fname[0] == 0)
            break;
        if(info.fattrib & AM_DIR)
            continue;
        const bool a2 = EndsWithA2nb(info.fname), gr = EndsWith(info.fname, ".grnb");
        if(!(a2 && s_count < kMaxFiles) && !(gr && s_nfilters < kMaxFilters))
            continue;

        char path[kPathLen];
        snprintf(path, sizeof(path), "%s%s", s_fsi.GetSDPath(), info.fname);

        if(a2)
        {
            Entry e;
            if(ReadHeader(path, e))
                s_entries[s_count++] = e;
        }
        else
        {
            FilterEntry fe;
            if(ReadFilterHeader(path, fe))
                s_filters[s_nfilters++] = fe;
        }
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

    for(int i = 1; i < s_nfilters; i++)  // the same, for the filters
    {
        FilterEntry key = s_filters[i];
        int         j   = i - 1;
        while(j >= 0 && strcmp(s_filters[j].path, key.path) > 0)
        {
            s_filters[j + 1] = s_filters[j];
            j--;
        }
        s_filters[j + 1] = key;
    }

    s_ready  = s_count > 0;
    s_status = s_ready ? "ok" : "no captures";
    return s_ready;
}

int FilterCount() { return s_nfilters; }

const FilterEntry* GetFilter(int index)
{
    return (index >= 0 && index < s_nfilters) ? &s_filters[index] : nullptr;
}

int FindFilter(const char* name)
{
    for(int i = 0; name && i < s_nfilters; i++)
        if(strcmp(s_filters[i].name, name) == 0)
            return i;
    return -1;
}

bool LoadFilterFile(int index, const uint8_t** data, size_t* bytes)
{
    const FilterEntry* e = GetFilter(index);
    if(!e || !data || !bytes)
        return false;
    if(f_open(&s_file, e->path, FA_READ) != FR_OK)
    {
        s_status = "open fail";
        return false;
    }
    // A layer or controls section may follow the network; only the header and network come here.
    const FSIZE_t size = (FSIZE_t)e->layer_offset;
    if(size < kGrnbHeaderBytes || size > kBounceBytes)
    {
        f_close(&s_file);
        s_status = "too big";
        return false;
    }
    UINT          got = 0;
    const FRESULT rr  = f_read(&s_file, s_bounce, (UINT)size, &got);
    f_close(&s_file);
    if(rr != FR_OK || got != size)
    {
        s_status = Fail("read", (int)rr);
        return false;
    }
    *data    = s_bounce;
    *bytes   = (size_t)size;
    s_status = "ok";
    return true;
}

bool LoadFilterControls(int index, FilterControls& c)
{
    const FilterEntry* e = GetFilter(index);
    if(!e || e->ctl_bytes < 16 || e->ctl_bytes > 16 + 16 * kMaxDialPoints + 4 * kOscFloats)
        return false;
    if(f_open(&s_file, e->path, FA_READ) != FR_OK)
        return false;
    UINT       got = 0;
    const bool ok  = f_lseek(&s_file, e->layer_offset + e->layer_bytes) == FR_OK
                    && f_read(&s_file, s_bounce, e->ctl_bytes, &got) == FR_OK && got == e->ctl_bytes;
    f_close(&s_file);
    const uint8_t* p = s_bounce;
    if(!ok || memcmp(p, "CTRL", 4) != 0 || (Rd16(p + 4) != 1 && Rd16(p + 4) != 2)
       || Crc32(p, e->ctl_bytes) != e->ctl_crc)
        return false;
    const int  n   = Rd16(p + 6);
    const bool osc = Rd16(p + 4) == 2;  // with an oscillator layer
    if(n < 1 || n > kMaxDialPoints || e->ctl_bytes != (uint32_t)(16 + (osc ? 16 * n + 4 * kOscFloats : 12 * n)))
        return false;
    auto f32 = [&](size_t o) {
        float    v;
        uint32_t b = Rd32(p + o);
        memcpy(&v, &b, 4);
        return v;
    };
    c.cut_lo = f32(8);
    c.cut_hi = f32(12);
    c.points = n;
    for(int i = 0; i < n; i++)
    {
        c.dial[i] = f32(16 + 4 * i);
        c.res[i]  = f32(16 + 4 * (n + i));
        c.fb[i]   = f32(16 + 4 * (2 * n + i));
        if(osc)
            c.osc_res[i] = f32(16 + 4 * (3 * n + i));
    }
    c.osc = osc;
    for(int k = 0; osc && k < kOscFloats; k++)
        c.osc_p[k] = f32(16 + 16 * n + 4 * k);
    return c.cut_hi != c.cut_lo;
}

bool LoadFilterLayer(int index, uint8_t* dst, size_t capacity, size_t* bytes)
{
    const FilterEntry* e = GetFilter(index);
    if(!e || !dst || !bytes || e->layer_bytes == 0 || e->layer_bytes > capacity)
    {
        s_status = "no layer";
        return false;
    }
    if(f_open(&s_file, e->path, FA_READ) != FR_OK)
    {
        s_status = "open fail";
        return false;
    }
    // Straight into dst: the card's DMA lands in SDRAM, and sd_diskio cleans and
    // invalidates whole cache lines around it, so the offset into a sector does not
    // matter as long as it is a multiple of 4, which every .grnb's layer offset is.
    UINT          got = 0;
    const FRESULT sr  = f_lseek(&s_file, e->layer_offset);
    const FRESULT rr  = sr == FR_OK ? f_read(&s_file, dst, (UINT)e->layer_bytes, &got) : sr;
    f_close(&s_file);
    if(rr != FR_OK || got != e->layer_bytes)
    {
        s_status = Fail("layer", (int)rr);
        return false;
    }
    if(Crc32(dst, e->layer_bytes) != e->layer_crc)
    {
        s_status = "layer crc";
        return false;
    }
    *bytes   = e->layer_bytes;
    s_status = "ok";
    return true;
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
