// multiosc_core — host / dispatcher
//
// Owns the Daisy Patch SM hardware, the OLED and B7, and runs one engine at a time over the panel agreed
// with David on 2026-11-22 (docs/PANEL.md, engine.h):
//   - it starts in the engine it was last left in;
//   - B7 tap: the engine's; hold and turn a pot: the second bank (Volume, -, Attack, Decay), with catch;
//     hold 2 s without turning: the next engine; held at power-up: the module's V/OCT calibration;
//   - the envelope is Joy's (common/joy_dsp.h): a drone until the first gate, then attack and decay;
//   - the engine, its tap state and its second bank are saved to QSPI (it runs BOOT_SRAM).
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include <cstring>
#include "daisy_patch_sm.h"
#include "oled_soft_i2c.h"
#include "engine.h"
#include "gestures.h"
#include "joy_dsp.h"
#include "legend_layout.h"
#include "unit_cal.h"

namespace multiosc {

class Host {
  public:
    static constexpr int kMaxEngines = 8;

    // Register an engine before Run(). Order = the order a long hold steps through.
    void AddEngine(Engine* e);

    // The firmware's version, shown under the power-up splash, e.g. "v2.0".
    void SetVersion(const char* v) { version_ = v; }

    // Init hardware and the OLED, then run the saved engine, switching on a long hold. Does not return.
    void Run();

    // --- internal (public so the static audio thunk can reach them) ---
    void ProcessAudio(daisy::AudioHandle::InputBuffer  in,
                      daisy::AudioHandle::OutputBuffer out,
                      size_t                           size);

  private:
    // Saved to QSPI: the engine, and each engine's tap state and second bank.
    struct Saved {
        uint32_t magic;
        uint32_t version;
        uint32_t engine;
        uint8_t  tap[kMaxEngines];
        float    volume[kMaxEngines];
        float    attack[kMaxEngines];
        float    decay[kMaxEngines];
        bool     operator!=(const Saved& o) const { return std::memcmp(this, &o, sizeof(Saved)) != 0; }
    };
    static constexpr uint32_t kSavedMagic   = 0x43534F4D; // "MOSC"
    static constexpr uint32_t kSavedVersion = 1;
    // Below the module's calibration (0x7D0000), Wish's settings (0x7E0000) and Joy's (0x7F0000)
    static constexpr uint32_t kSavedOffset = 0x7C0000;

    void InitHardware();
    void RunCalibration(); // B7 held at power-up: the unit's two-point V/OCT calibration
    void LoadSaved();
    void StoreCurrent(); // the running engine's state into the saved record (not yet to flash)
    void StartEngine(int index, bool button_down);
    void UpdateButton(const float* knobs); // gestures, from ProcessAudio
    void DrawLegend();                     // from the UI loop
    void DrawSplash();

    daisy::patch_sm::DaisyPatchSM    hw_;
    oled::SSD1306                    display_;
    daisy::Switch                    button_;
    unitcal::Store                   unit_cal_{hw_.qspi}; // the unit's V/OCT calibration (unit_cal.h)
    daisy::PersistentStorage<Saved>  saved_{hw_.qspi};
    joy::AdEnvelope                  env_;

    const char* version_ = nullptr;

    Engine* engines_[kMaxEngines] = {nullptr};
    int     engine_count_         = 0;
    int     active_index_         = 0;
    Engine* active_               = nullptr;

    // Pots: the main page's values and the second bank's, each with catch.
    SoftTakeover main_[4];
    SoftTakeover layer_[4];
    float        volume_ = 0.8f, attack_ = 0.05f, decay_ = 0.25f;
    float        gain_   = 0.0f; // volume_ squared, smoothed per sample so turning it doesn't zipper

    // B7, decoded in the audio callback (gestures.h)
    Gestures      gestures_;
    volatile bool tap_pending_    = false;
    volatile bool next_requested_ = false;
    volatile bool layer_shown_    = false; // for the legend
    volatile bool settings_dirty_ = false;
    uint32_t      dirty_since_    = 0;
    bool          gated_          = false; // the first gate has ended the drone
    bool          prev_gate_      = false;

    static constexpr uint32_t kSaveDelayMs = 3000;

    // Redraw the OLED only when the legend changes, so soft-I2C isn't bit-banged
    // continuously (that switching couples into the codec as audible noise).
    const Engine* drawn_engine_ = nullptr;
    const char*   drawn_[6]     = {nullptr};
    bool          drawn_layer_  = false;
    bool          drawn_valid_  = false;
};

} // namespace multiosc
