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
//
// Version 2 (2026-11-21) adds the FM jack's zero: GetAdcValue(CV_8) with nothing patched, measured in the
// same run by the apps with an FM input (Joy, Joy Lite). Each unit's input reads a little off 0 V, and at
// FM's 6 semitones a volt every 10 mV of it detuned the unit by 6 cents. A version 1 record keeps its V/OCT
// calibration and has no FM zero until the next calibration.

#pragma once
#include <cmath>
#include <cstdint>
#include "daisy_patch_sm.h"
#include "voct_cal.h"

namespace unitcal {

constexpr uint32_t kQspiOffset = 0x7D0000;
constexpr uint32_t kMagic      = 0x4C414356;  // "VCAL"
constexpr uint32_t kVersion    = 2;
// 0.5 V: past this the FM jack was patched during the capture. An empty input can read a few hundredths:
// Joy's default calibration, converted to libDaisy v9, puts that unit's 0 V at -0.025 (voct_cal.h).
constexpr float    kMaxFmZero  = joy::kMaxCenter;

struct Record
{
    uint32_t magic;
    uint32_t version;
    uint32_t measured;  // 1 once a calibration has been captured, or carried over, on this unit
    float    voct_center;
    float    voct_slope;
    uint32_t fm_measured;  // 1 once the FM jack's zero has been captured on this unit (version 2)
    float    fm_zero;
    bool     operator!=(const Record& r) const
    {
        return magic != r.magic || version != r.version || measured != r.measured
               || voct_center != r.voct_center || voct_slope != r.voct_slope
               || fm_measured != r.fm_measured || fm_zero != r.fm_zero;
    }
};

class Store
{
  public:
    explicit Store(daisy::QSPIHandle& qspi) : storage_(qspi) {}

    // Reads the record, starting a blank one on a unit that has never had it, and bringing a version 1
    // record up to date (its last two fields read as erased flash).
    void Init()
    {
        const Record blank{kMagic, kVersion, 0, 0.f, 0.f, 0, 0.f};
        storage_.Init(blank, kQspiOffset);
        Record& r = storage_.GetSettings();
        if(r.magic == kMagic && r.version == 1)
        {
            r.version     = kVersion;
            r.fm_measured = 0;
            r.fm_zero     = 0.f;
            storage_.Save();
        }
        else if(r.magic != kMagic || r.version != kVersion)
            storage_.RestoreDefaults();
    }

    bool Measured() const { return Get().measured == 1; }

    // The unit's calibration, or the app's own default when none has been measured here.
    joy::VoctCal Load(const joy::VoctCal& app_default) const
    {
        const Record& r = Get();
        return r.measured == 1 ? joy::VoctCal{r.voct_center, r.voct_slope} : app_default;
    }

    // The FM jack's reading with nothing patched, to take off every reading; 0 until measured.
    float FmZero() const
    {
        const Record& r = Get();
        return r.fm_measured == 1 && std::isfinite(r.fm_zero) ? r.fm_zero : 0.f;
    }

    // A measured calibration. `fm_zero`: the empty FM jack's reading from the same run, from the apps with an
    // FM input; the others leave it out and keep what is stored.
    void Save(const joy::VoctCal& c, float fm_zero = NAN)
    {
        Record& r     = storage_.GetSettings();
        r.magic       = kMagic;
        r.version     = kVersion;
        r.measured    = 1;
        r.voct_center = c.center;
        r.voct_slope  = c.slope;
        if(std::isfinite(fm_zero))
        {
            r.fm_measured = 1;
            r.fm_zero     = fm_zero;
        }
        storage_.Save();
    }

    // One-time carry-over: an app that kept its own calibration before this record existed hands it over,
    // if it differs from that app's own compiled-in values of then (so it was measured) and nothing is here
    // yet. Those were all measured on libDaisy v8, so it is converted to today's scaling (voct_cal.h).
    void Adopt(const joy::VoctCal& old_cal, const joy::VoctCal& old_default)
    {
        if(Measured())
            return;
        if(old_cal.center == old_default.center && old_cal.slope == old_default.slope)
            return;
        const joy::VoctCal c = joy::FromLibDaisy8(old_cal);
        if(std::fabs(c.slope / joy::kNominalSlope - 1.0f) > joy::kSlopeTolerance
           || std::fabs(c.center) > joy::kMaxCenter)
            return;
        Save(c);
    }

  private:
    const Record& Get() const { return const_cast<daisy::PersistentStorage<Record>&>(storage_).GetSettings(); }

    daisy::PersistentStorage<Record> storage_;
};

}  // namespace unitcal
