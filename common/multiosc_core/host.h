// multiosc_core — host / dispatcher
//
// Owns the Daisy Patch SM hardware, the OLED, and the single B7 button. On boot
// it shows a firmware chooser; once an engine is selected it starts audio and
// runs the UI loop, dispatching audio + B7 gestures (short = cycle, long =
// Play/Edit page) to the active engine and drawing its control legend.
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#pragma once

#include "daisy_patch_sm.h"
#include "oled_soft_i2c.h"
#include "engine.h"

namespace multiosc {

class Host {
  public:
    static constexpr int kMaxEngines = 8;

    // Register an engine before Run(). Order = boot-menu order.
    void AddEngine(Engine* e);

    // Init hardware + OLED, run the boot chooser, start audio, run UI loop.
    // Does not return.
    void Run();

    // --- internal (public so the static audio thunk can reach them) ---
    void ProcessAudio(daisy::AudioHandle::InputBuffer  in,
                      daisy::AudioHandle::OutputBuffer out,
                      size_t                           size);

  private:
    void InitHardware();
    int  RunBootMenu();   // returns selected engine index
    void DrawMenu(int sel);
    void UpdateButton();  // gesture decode, called from ProcessAudio
    void DrawLegend();    // called from the UI loop

    daisy::patch_sm::DaisyPatchSM hw_;
    oled::SSD1306                 display_;
    daisy::Switch                 button_;

    Engine* engines_[kMaxEngines] = {nullptr};
    int     engine_count_         = 0;
    Engine* active_               = nullptr;

    EngineContext ctx_;
    volatile bool short_press_pending_ = false;
    bool          hold_handled_        = false; // long-press consumed this hold
    static constexpr uint32_t kLongPressMs = 600;

    // Redraw the OLED only when the legend changes, so soft-I2C isn't bit-banged
    // continuously (that switching couples into the codec as audible noise).
    const Engine* drawn_engine_ = nullptr;
    const char*   drawn_sel_    = nullptr;
    bool          drawn_edit_   = false;
    bool          drawn_valid_  = false;
};

} // namespace multiosc
