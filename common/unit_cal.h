// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Baghurst
//
// unit_cal.h — one V/OCT calibration per patch.Init() unit, shared by every app that reads V/OCT on CV_5
// (Joy, Joy Lite, Wish on patch.Init(), MultiOsc). Calibration describes the unit's input, not the app,
// so a unit calibrated once tracks in each of them, and swapping apps by SD card keeps it (David,
// 2026-11-21).
//
// Where it lives. The Patch SM has no EEPROM: its only storage is the 8 MB QSPI flash the program sits
// on. The Daisy bootloader stores a program from 0x40000 and erases "in 64kB chunks" only as far as the
// program reaches (libDaisy doc/md/_a7_Getting-Started-Daisy-Bootloader.md), so a block far above any
// program survives every SD update. This record has 0x7D0000 to itself: below Wish's settings
// (0x7E0000) and Joy's and Joy Lite's (0x7F0000); the largest program is under 300 KB. A save erases
// only the record's own 4 KB sector (libDaisy QSPIHandle::Erase). Writing QSPI needs the app to run
// from SRAM (APP_TYPE = BOOT_SRAM).
//
// The calibration itself is voct_cal.h's two-point line, in the raw GetAdcValue(CV_5) domain every app
// already reads: semitones above 0 V = (adc - center) * slope. With nothing measured on this unit, an
// app keeps its own defaults.

#pragma once
#include <cstdint>
#include "daisy_patch_sm.h"
#include "voct_cal.h"

namespace unitcal {

constexpr uint32_t kQspiOffset = 0x7D0000;
constexpr uint32_t kMagic      = 0x4C414356;  // "VCAL"
constexpr uint32_t kVersion    = 1;

struct Record
{
    uint32_t magic;
    uint32_t version;
    uint32_t measured;  // 1 once a calibration has been captured, or carried over, on this unit
    float    voct_center;
    float    voct_slope;
    bool     operator!=(const Record& r) const
    {
        return magic != r.magic || version != r.version || measured != r.measured
               || voct_center != r.voct_center || voct_slope != r.voct_slope;
    }
};

class Store
{
  public:
    explicit Store(daisy::QSPIHandle& qspi) : storage_(qspi) {}

    // Reads the record, starting a blank one on a unit that has never had it.
    void Init()
    {
        const Record blank{kMagic, kVersion, 0, 0.f, 0.f};
        storage_.Init(blank, kQspiOffset);
        const Record& r = storage_.GetSettings();
        if(r.magic != kMagic || r.version != kVersion)
            storage_.RestoreDefaults();
    }

    bool Measured() const { return Get().measured == 1; }

    // The unit's calibration, or the app's own default when none has been measured here.
    joy::VoctCal Load(const joy::VoctCal& app_default) const
    {
        const Record& r = Get();
        return r.measured == 1 ? joy::VoctCal{r.voct_center, r.voct_slope} : app_default;
    }

    void Save(const joy::VoctCal& c)
    {
        Record& r     = storage_.GetSettings();
        r.magic       = kMagic;
        r.version     = kVersion;
        r.measured    = 1;
        r.voct_center = c.center;
        r.voct_slope  = c.slope;
        storage_.Save();
    }

    // One-time carry-over: an app that kept its own calibration before this record existed hands it
    // over, if it differs from that app's defaults (so it was measured) and nothing is here yet.
    void Adopt(const joy::VoctCal& old_cal, const joy::VoctCal& app_default)
    {
        if(Measured())
            return;
        if(old_cal.center == app_default.center && old_cal.slope == app_default.slope)
            return;
        if(old_cal.slope < 30.f || old_cal.slope > 120.f)  // voct_cal.h's sanity range
            return;
        Save(old_cal);
    }

  private:
    const Record& Get() const { return const_cast<daisy::PersistentStorage<Record>&>(storage_).GetSettings(); }

    daisy::PersistentStorage<Record> storage_;
};

}  // namespace unitcal
