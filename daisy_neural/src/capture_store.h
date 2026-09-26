/**
 * capture_store — .a2nb captures from the microSD card.
 *
 * Scans the card root for *.a2nb, reads each 32-byte header for its name and
 * output gain, and loads weights on demand. Format is written by
 * tools/export_captures.py; see the README.
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
constexpr int    kMaxFiles = 8;

// Must match tools/export_captures.py.
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

/** Mount the card and scan for captures. Safe to call when no card is present;
 *  returns false and Status() says how far it got. */
bool Init();

int          Count();
const Entry* Get(int index);

/** Read one capture's weights into dst. Verifies magic, version, count and
 *  CRC32 before touching dst, so a corrupt card cannot feed garbage to the
 *  network. Returns false and leaves dst untouched on any failure. */
bool Load(int index, float* dst, size_t dst_capacity);

/** Where Init() or the last Load() got to — for the display, so a card problem
 *  reads as a card problem rather than a dead engine. */
const char* Status();

} // namespace captures
