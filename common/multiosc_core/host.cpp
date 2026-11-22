// multiosc_core — host / dispatcher implementation.
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#include "host.h"
#include <cmath>
#include <cstdio>

using namespace daisy;
using namespace daisy::patch_sm;

namespace multiosc {

// Single instance reachable from the static audio callback thunk.
static Host* g_host = nullptr;

static void AudioThunk(AudioHandle::InputBuffer  in,
                       AudioHandle::OutputBuffer out,
                       size_t                    size)
{
    if(g_host)
        g_host->ProcessAudio(in, out, size);
}

static float Clamp01(float x)
{
    return x < 0.f ? 0.f : x > 1.f ? 1.f : x;
}

// A saved value, or the default if it isn't one (erased flash reads as NaN).
static float Valid01(float x, float fallback)
{
    return std::isfinite(x) && x >= 0.f && x <= 1.f ? x : fallback;
}

// The module's V/OCT calibration (unit_cal.h). Until one is measured: Joy's defaults, in libDaisy v9's terms.
static const joy::VoctCal kDefaultCal = joy::FromLibDaisy8({0.007074f, 60.73f});
static joy::VoctCal       g_voct_cal  = kDefaultCal;

// Joy's attack and decay law: 1 ms to 6 s, squared so the short end has room.
static float EnvMs(float x)
{
    return 1.0f + x * x * 6000.0f;
}

void Host::AddEngine(Engine* e)
{
    if(e && engine_count_ < kMaxEngines)
        engines_[engine_count_++] = e;
}

void Host::InitHardware()
{
    hw_.Init();

    const float sr = hw_.AudioSampleRate();
    button_.Init(hw_.B7, sr, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);

    // OLED on soft I2C via the expansion header (A2 = SDA, A3 = SCL).
    display_.Init(hw_.A2, hw_.A3);
    display_.Clear();
    // 64x48: two large lines of 14 px, then the version in the small font
    display_.DrawStringLargeCentered(2, "MULTI", false);
    display_.DrawStringLargeCentered(18, "OSC", false);
    if(version_)
        display_.DrawStringCentered(38, version_, false);
    display_.Update();
    System::Delay(700);

    hw_.StartAdc();

    // V/OCT calibration: the unit's (unit_cal.h), so one calibration serves every app on this unit that reads
    // V/OCT. MultiOsc runs from SRAM (APP_TYPE = BOOT_SRAM) so that it can save one. B7 held at power-up runs
    // the two-point routine, as on Joy.
    unit_cal_.Init();
    g_voct_cal = unit_cal_.Load(kDefaultCal);
    for(int i = 0; i < 40; i++)
    {
        hw_.ProcessAllControls();
        button_.Debounce();
        System::Delay(1);
    }
    if(button_.Pressed())
        RunCalibration();
}

void Host::RunCalibration()
{
    auto prompt = [this](const char* volts) {
        display_.Clear();
        display_.DrawStringCentered(0, "CALIBRATE", false);
        display_.DrawStringLargeCentered(15, volts, false);
        display_.DrawStringCentered(38, "patch+B7", false);
        display_.Update();
    };
    // a press that starts after this point: let go first, then press
    auto fresh_press = [this]() {
        do { hw_.ProcessAllControls(); button_.Debounce(); System::Delay(1); } while(button_.Pressed());
        do { hw_.ProcessAllControls(); button_.Debounce(); System::Delay(1); } while(!button_.Pressed());
    };
    auto capture = [this]() {
        constexpr int kN  = 200;
        float         acc = 0.f;
        for(int i = 0; i < kN; i++)
        {
            hw_.ProcessAllControls();
            acc += hw_.GetAdcValue(CV_VOCT);
            System::Delay(1);
        }
        return acc / kN;
    };

    prompt("1V");
    fresh_press();
    const float adc_1v = capture();
    prompt("3V");
    fresh_press();
    const float adc_3v = capture();

    // A capture that doesn't make sense (nothing patched, a slip) saves nothing and says so.
    joy::VoctCal cal;
    display_.Clear();
    if(joy::ComputeVoctCal(adc_1v, adc_3v, cal))
    {
        unit_cal_.Save(cal);
        g_voct_cal = cal;
        // what it corrects: the input's octave against the nominal law, in cents
        char line[16];
        snprintf(line, sizeof(line), "OCT %+dc", joy::CentsPerOctaveFromNominal(cal));
        display_.DrawStringLargeCentered(6, "DONE", false);
        display_.DrawStringCentered(30, line, false);
    }
    else
    {
        display_.DrawStringLargeCentered(4, "FAIL", false);
        display_.DrawStringCentered(26, "not saved", false);
        display_.DrawStringCentered(38, "redo 1V/3V", false);
    }
    display_.Update();
    System::Delay(2400);
    // let go before the engine starts, so the release isn't read as a tap
    do { hw_.ProcessAllControls(); button_.Debounce(); System::Delay(1); } while(button_.Pressed());
    System::Delay(600);
}

void Host::LoadSaved()
{
    Saved d;
    std::memset(&d, 0, sizeof(d));
    d.magic   = kSavedMagic;
    d.version = kSavedVersion;
    d.engine  = 0;
    for(int e = 0; e < kMaxEngines; e++)
    {
        d.tap[e]    = 0;
        d.volume[e] = 0.8f;
        d.attack[e] = 0.05f;
        d.decay[e]  = e < engine_count_ ? engines_[e]->DefaultDecay() : 0.25f;
    }
    saved_.Init(d, kSavedOffset);
    const Saved& s = saved_.GetSettings();
    if(s.magic != kSavedMagic || s.version != kSavedVersion)
        saved_.RestoreDefaults();
}

void Host::StoreCurrent()
{
    Saved& s                 = saved_.GetSettings();
    s.engine                 = static_cast<uint32_t>(active_index_);
    s.tap[active_index_]     = active_->TapState();
    s.volume[active_index_]  = volume_;
    s.attack[active_index_]  = attack_;
    s.decay[active_index_]   = decay_;
}

void Host::StartEngine(int index, bool button_down)
{
    const Saved& s = saved_.GetSettings();
    active_index_  = index;
    active_        = engines_[index];

    volume_ = Valid01(s.volume[index], 0.8f);
    attack_ = Valid01(s.attack[index], 0.05f);
    decay_  = Valid01(s.decay[index], active_->DefaultDecay());
    gain_   = volume_ * volume_;

    const float sr = hw_.AudioSampleRate();
    hw_.SetAudioBlockSize(active_->BlockSize());
    active_->Init(sr);
    active_->SetTapState(s.tap[index]);

    // A fresh start: the envelope drones until the first gate, and the pots take over as they stand.
    env_.Init(sr);
    gated_     = false;
    prev_gate_ = false;
    hw_.ProcessAllControls();
    for(int p = 0; p < 4; p++)
        main_[p].Set(hw_.GetAdcValue(POT1 + p));

    pressed_        = false;
    ignore_press_   = button_down; // the hold that switched engine is still down
    turned_         = false;
    switched_       = false;
    tap_pending_    = false;
    next_requested_ = false;
    layer_shown_    = false;
    drawn_valid_    = false;
}

void Host::UpdateButton(const float* knobs)
{
    button_.Debounce();
    const bool down = button_.Pressed();

    if(ignore_press_)
    {
        if(!down)
            ignore_press_ = false;
        layer_shown_ = false;
        return;
    }

    if(button_.RisingEdge())
    {
        pressed_  = true;
        turned_   = false;
        switched_ = false;
        for(int p = 0; p < 4; p++)
            press_knob_[p] = knobs[p];
    }

    if(pressed_ && down)
    {
        if(!turned_ && !switched_)
        {
            for(int p = 0; p < 4; p++)
                if(std::fabs(knobs[p] - press_knob_[p]) > kTurnThresh)
                    turned_ = true;
            if(turned_)
            {
                // the second bank, each knob catching its value before it moves it
                layer_[0].Reset(volume_);
                layer_[1].Reset(0.5f);
                layer_[2].Reset(attack_);
                layer_[3].Reset(decay_);
            }
            else if(button_.TimeHeldMs() >= kNextMs)
            {
                switched_       = true;
                next_requested_ = true;
            }
        }
        layer_shown_ = !switched_ && (turned_ || button_.TimeHeldMs() >= kShowLayerMs);
    }
    else if(pressed_ && !down)
    {
        if(turned_)
            for(int p = 0; p < 4; p++)
                main_[p].Reset(main_[p].value()); // back on the main page, the knobs catch again
        else if(!switched_)
            tap_pending_ = true;
        pressed_     = false;
        turned_      = false;
        layer_shown_ = false;
    }
}

void Host::ProcessAudio(AudioHandle::InputBuffer  in,
                        AudioHandle::OutputBuffer out,
                        size_t                    size)
{
    (void)in;
    hw_.ProcessAllControls();
    float knobs[4];
    for(int p = 0; p < 4; p++)
        knobs[p] = hw_.GetAdcValue(POT1 + p);
    UpdateButton(knobs);

    if(!active_ || next_requested_)
    {
        for(size_t i = 0; i < size; i++)
            out[0][i] = out[1][i] = 0.0f; // silence while switching engine
        return;
    }

    if(pressed_ && turned_)
    {
        // the second bank: pot 1 Volume, pot 2 free, pot 3 Attack, pot 4 Decay
        const float v = layer_[0].Process(knobs[0]);
        layer_[1].Process(knobs[1]);
        const float a = layer_[2].Process(knobs[2]);
        const float d = layer_[3].Process(knobs[3]);
        if(v != volume_ || a != attack_ || d != decay_)
        {
            volume_         = v;
            attack_         = a;
            decay_          = d;
            settings_dirty_ = true;
            dirty_since_    = System::GetNow();
        }
    }
    else if(!pressed_)
        for(int p = 0; p < 4; p++)
            main_[p].Process(knobs[p]); // held while B7 is down, so a tap or a hold moves nothing

    Controls c;
    c.pot[0] = main_[0].value();
    for(int p = 1; p < 4; p++)
        c.pot[p] = Clamp01(main_[p].value() + 0.5f * hw_.GetAdcValue(CV_POT2 + p - 1)); // +-5 V: +-half the knob
    c.decay = decay_;
    c.note  = kBaseNote + joy::VoctToSemitones(g_voct_cal, hw_.GetAdcValue(CV_VOCT));
    c.gate  = hw_.gate_in_1.State() || hw_.gate_in_1.Trig();
    if(c.gate && !prev_gate_)
        gated_ = true;
    prev_gate_ = c.gate;
    c.gated    = gated_;
    c.tap      = tap_pending_;
    tap_pending_ = false;

    active_->Process(c, out, size);

    if(c.tap && active_->TapSelection())
    {
        settings_dirty_ = true;
        dirty_since_    = System::GetNow();
    }

    const bool use_env = active_->UsesEnvelope();
    env_.SetAttackDecayMs(EnvMs(attack_), EnvMs(decay_));
    env_.SetGate(c.gate);
    const float target = volume_ * volume_;
    for(size_t i = 0; i < size; i++)
    {
        gain_ += 0.002f * (target - gain_);
        const float g = gain_ * (use_env ? env_.Process() : 1.0f);
        out[0][i] *= g;
        out[1][i] *= g;
    }
}

void Host::DrawSplash()
{
    display_.Clear();
    display_.DrawStringLargeCentered(16, active_->Name(), false);
    display_.Update();
    drawn_valid_ = false;
}

void Host::DrawLegend()
{
    // "NAME:TAP" title with an underline, then a 2x2 grid laid out as the pots sit:
    //     pot 1's choice   pot 2
    //     pot 3            pot 4
    // While B7 is held, the second bank instead: Vol -, Atk Dec.
    const bool  layer  = layer_shown_;
    const char* tapsel = active_->TapSelection();
    const char* cell[4];
    if(layer)
    {
        cell[0] = "Vol";
        cell[1] = "-";
        cell[2] = active_->UsesEnvelope() ? "Atk" : "-";
        cell[3] = active_->DecayLabel();
    }
    else
    {
        cell[0] = active_->Choice();
        cell[1] = active_->PotLabel(1);
        cell[2] = active_->PotLabel(2);
        cell[3] = active_->PotLabel(3);
    }

    // Skip the (slow, noisy) soft-I2C redraw when nothing on screen changed.
    if(drawn_valid_ && active_ == drawn_engine_ && layer == drawn_layer_ && tapsel == drawn_[4]
       && cell[0] == drawn_[0] && cell[1] == drawn_[1] && cell[2] == drawn_[2] && cell[3] == drawn_[3])
        return;
    drawn_engine_ = active_;
    drawn_layer_  = layer;
    for(int i = 0; i < 4; i++)
        drawn_[i] = cell[i];
    drawn_[4]    = tapsel;
    drawn_valid_ = true;

    // Title, rows and selection line: see legend_layout.h, which tools/panel_check asserts against.
    const LegendText lt = ComposeLegend(active_->Name(), tapsel, nullptr, layer);
    char             row1[16], row2[16];
    ComposeGridRow(row1, sizeof(row1), cell[0], cell[1]);
    ComposeGridRow(row2, sizeof(row2), cell[2], cell[3]);

    display_.Clear();
    display_.DrawStringCentered(0, lt.title, false);
    display_.DrawHLine(0, 9, 64);
    display_.DrawString(0, kGridRow1Y, row1, false);
    display_.DrawString(0, kGridRow2Y, row2, false);
    if(lt.has_selection())
        display_.DrawStringCentered(kSelectionY, lt.selection, false);
    display_.Update();
}

void Host::Run()
{
    g_host = this;
    InitHardware();
    if(engine_count_ == 0)
        while(true) {}
    LoadSaved();

    int start = static_cast<int>(saved_.GetSettings().engine);
    if(start < 0 || start >= engine_count_)
        start = 0;
    button_.Debounce();
    StartEngine(start, button_.Pressed());
    DrawSplash();
    uint32_t splash_until = System::GetNow() + 900;
    hw_.StartAudio(AudioThunk);

    uint32_t next_draw = 0;
    while(true)
    {
        const uint32_t now = System::GetNow();

        if(next_requested_)
        {
            // the callback has gone silent: let a few blocks of it out, then change engine
            System::Delay(10);
            hw_.StopAudio();
            StoreCurrent();
            StartEngine((active_index_ + 1) % engine_count_, true);
            settings_dirty_ = true;
            dirty_since_    = System::GetNow();
            DrawSplash();
            splash_until = System::GetNow() + 900;
            hw_.StartAudio(AudioThunk);
            continue;
        }

        if((int32_t)(now - splash_until) >= 0 && (int32_t)(now - next_draw) >= 0)
        {
            DrawLegend();
            next_draw = now + 50; // ~20 fps; bit-banged I2C is slow
        }

        // Debounced persistence, from here rather than the callback: the QSPI erase blocks for tens of ms,
        // and the audio interrupt keeps running from SRAM meanwhile.
        if(settings_dirty_ && now - dirty_since_ > kSaveDelayMs)
        {
            settings_dirty_ = false;
            StoreCurrent();
            saved_.Save(); // writes only if something changed
        }
        System::Delay(2);
    }
}

} // namespace multiosc
