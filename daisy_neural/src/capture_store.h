/**
 * capture_store — .a2nb captures and .grnb filters from the microSD card.
 *
 * Scans the card root for *.a2nb, reads each 32-byte header for its name and
 * output gain, and loads weights on demand. Format is written by
 * tools/nam_to_a2nb.py (format in tools/a2nb.py); see the README. The same scan
 * finds *.grnb, the GRU filters of the FILTERS bank (tools/gru_export.py).
 *
 * Nothing here is real-time safe. Loading reads a file and the caller then runs
 * the engine's prewarm(), so it belongs in the main loop with audio muted,
 * never in the audio callback.
 */

#pragma once

#include "daisy_patch_sm.h"
#include <cstddef>
#include <cstdint>

namespace captures
{

// Ten characters is the panel budget; the format allows 11 plus a NUL.
constexpr size_t kNameLen  = 12;
constexpr size_t kPathLen  = 64;
constexpr int    kMaxFiles = 32;  // room for more amps on the card

// Must match tools/a2nb.py.
constexpr uint32_t kMagic       = 0x424E3241;  // 'A2NB' little-endian
constexpr uint16_t kVersion     = 1;
constexpr size_t   kHeaderBytes = 32;

struct Entry
{
    char  name[kNameLen + 1];
    char  path[kPathLen];
    float gain;
    int   weight_count;
};

/** A capture's A-weighted level in dB at gain 1 on nam_import's phrase, run through the
 *  engine (main.cpp), and the display's progress line; both for ImportNam. */
using LevelFn    = float (*)(const float* weights);
using ProgressFn = void (*)(int done, int total, const char* file);

struct ImportResult
{
    int         added   = 0;
    int         refused = 0;
    char        last_refused[kNameLen + 1] = "";  // the start of its file name
    const char* last_why                   = "";
};

/** Mount the card, import any .nam files on it (when level is given: see ImportNam) and
 *  scan for captures. Safe to call when no card is present; returns false and Status()
 *  says how far it got. */
bool Init(LevelFn level = nullptr, ProgressFn progress = nullptr, ImportResult* imported = nullptr);

/** Every .nam in the card's root becomes an .a2nb beside it, as nam_to_a2nb.py and
 *  level_captures.py would have written it (nam_import.h), so a TONE3000 download can go
 *  straight onto the card. The .nam then moves into nam_done/, or into nam_refused/ if it
 *  is not an A2 capture, so each is imported once; nothing is deleted. A name already
 *  taken gets a number ("Ampeg SVT 2") instead of overwriting. Called by Init between the
 *  mount and the scan; slow (a few seconds a file, mostly the levelling). */
ImportResult ImportNam(LevelFn level, ProgressFn progress);

int          Count();
const Entry* Get(int index);

/** Index of the capture with this name (the name in its header), or -1. */
int Find(const char* name);

/** Read one capture's weights into dst. Verifies magic, version, count and
 *  CRC32 before touching dst, so a corrupt card cannot feed garbage to the
 *  network. Returns false and leaves dst untouched on any failure. */
bool Load(int index, float* dst, size_t dst_capacity);

/** Where Init() or the last Load() got to — for the display, so a card problem
 *  reads as a card problem rather than a dead engine. */
const char* Status();

// ---------------------------------------------------------------------------
// FILTERS: GRU networks, .grnb files (tools/grnb.py; the engine is nam/gru_runtime.h)
// ---------------------------------------------------------------------------
constexpr uint32_t kGrnbMagic       = 0x424E5247;  // 'GRNB' little-endian
constexpr size_t   kGrnbHeaderBytes = 64;
constexpr int      kMaxFilters      = 16;

struct FilterEntry
{
    char     name[kNameLen + 1];
    char     path[kPathLen];
    float    gain;
    int      hidden;
    int      inputs;
    uint32_t layer_bytes;   // version 2: the granular layer's section, after the payload; else 0
    uint32_t layer_offset;  // where it starts in the file
    uint32_t layer_crc;
};

int                FilterCount();
const FilterEntry* GetFilter(int index);
int                FindFilter(const char* name);

/** Read one .grnb's header and network (not its layer) into an internal buffer and point
 *  data at it, for gru::Parse() to check (magic, sizes, CRC32) and copy into a network.
 *  The buffer is reused by the next load. Main loop only, with the network silent. */
bool LoadFilterFile(int index, const uint8_t** data, size_t* bytes);

/** Read a version-2 .grnb's layer section (nam/gran_layer.h) into dst, a 32-byte-aligned
 *  SDRAM buffer of at least the entry's layer_bytes, and check its CRC32. False on any
 *  failure, leaving dst's contents unspecified. Main loop only, with the layer silent. */
bool LoadFilterLayer(int index, uint8_t* dst, size_t capacity, size_t* bytes);

} // namespace captures
