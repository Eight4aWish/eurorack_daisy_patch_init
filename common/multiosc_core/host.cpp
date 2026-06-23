// multiosc_core — host / dispatcher implementation.
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#include "host.h"
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
    display_.DrawStringLargeCentered(8, "MULTI", false);
    display_.DrawStringLargeCentered(28, "OSC", false);
    display_.Update();
    System::Delay(700);

    hw_.StartAdc();
}

void Host::DrawMenu(int sel)
{
    char line[24];
    display_.Clear();
    display_.DrawStringCentered(0, "SELECT", false);
    for(int i = 0; i < engine_count_; i++)
    {
        const uint8_t y = 12 + i * 9;
        std::snprintf(line, sizeof(line), "%s %s", i == sel ? ">" : " ",
                      engines_[i]->Name());
        display_.DrawString(2, y, line, false);
    }
    display_.Update();
}

int Host::RunBootMenu()
{
    if(engine_count_ <= 1)
        return 0;

    int sel = 0, last = -1;
    button_.Debounce(); // prime; ignore a press that's held from power-on

    while(true)
    {
        hw_.ProcessAllControls();
        button_.Debounce();

        int s = (int)(hw_.GetAdcValue(KNOB_TUNE) * engine_count_);
        if(s < 0)
            s = 0;
        if(s >= engine_count_)
            s = engine_count_ - 1;
        sel = s;

        if(sel != last)
        {
            DrawMenu(sel);
            last = sel;
        }
        if(button_.RisingEdge())
            break; // short press selects

        System::Delay(1);
    }
    return sel;
}

void Host::UpdateButton()
{
    button_.Debounce();
    ctx_.page_changed = false;

    if(button_.RisingEdge())
        hold_handled_ = false;

    if(button_.Pressed() && !hold_handled_
       && button_.TimeHeldMs() >= kLongPressMs)
    {
        ctx_.edit_page    = !ctx_.edit_page;
        ctx_.page_changed = true;
        hold_handled_     = true; // long press consumed; no short on release
    }

    if(button_.FallingEdge() && !hold_handled_)
        short_press_pending_ = true;
}

void Host::ProcessAudio(AudioHandle::InputBuffer  in,
                        AudioHandle::OutputBuffer out,
                        size_t                    size)
{
    hw_.ProcessAllControls();
    UpdateButton();

    // On the Edit page the TUNE knob toggles the amplitude envelope on/off.
    if(ctx_.edit_page)
        env_on_ = hw_.GetAdcValue(KNOB_TUNE) >= 0.5f;

    EngineContext ctx  = ctx_;
    ctx.short_press      = short_press_pending_;
    ctx.env_on           = env_on_;
    short_press_pending_ = false;

    if(active_)
        active_->Process(hw_, ctx, in, out, size);
}

void Host::DrawLegend()
{
    // Matches the Braids/MultiFX layout: centered "NAME:SEL" title with an
    // underline, then a 2x2 grid of the four knob labels positioned like the
    // physical pots:
    //     Tune   <MOD1>
    //     <MOD2> <MOD3>
    const bool  edit = ctx_.edit_page;
    const char* sel  = active_->Selection();
    const char* l0   = active_->ModLabel(0, edit);
    const char* l1   = active_->ModLabel(1, edit);
    const char* l2   = active_->ModLabel(2, edit);

    // Skip the (slow, noisy) soft-I2C redraw when nothing on screen changed.
    if(drawn_valid_ && active_ == drawn_engine_ && edit == drawn_edit_
       && env_on_ == drawn_env_ && sel == drawn_sel_ && l0 == drawn_l_[0]
       && l1 == drawn_l_[1] && l2 == drawn_l_[2])
        return;
    drawn_engine_ = active_;
    drawn_edit_   = edit;
    drawn_env_    = env_on_;
    drawn_sel_    = sel;
    drawn_l_[0]   = l0;
    drawn_l_[1]   = l1;
    drawn_l_[2]   = l2;
    drawn_valid_  = true;

    const char* tag = edit ? "EDIT" : sel;
    char        title[24], row1[16], row2[16];

    if(tag && tag[0])
        std::snprintf(title, sizeof(title), "%s:%s", active_->Name(), tag);
    else
        std::snprintf(title, sizeof(title), "%s", active_->Name());

    // Top-left cell is the pitch knob in Play, the envelope on/off in Edit.
    const char* tl = edit ? (env_on_ ? "EnvOn" : "EnvOf") : "Tune";
    std::snprintf(row1, sizeof(row1), "%-6s%s", tl, l0);
    std::snprintf(row2, sizeof(row2), "%-6s%s", l1, l2);

    display_.Clear();
    display_.DrawStringCentered(0, title, false);
    display_.DrawHLine(0, 9, 64);
    display_.DrawString(0, 13, row1, false);
    display_.DrawString(0, 23, row2, false);
    display_.Update();
}

void Host::Run()
{
    g_host = this;
    InitHardware();

    const int sel = RunBootMenu();
    active_        = engines_[sel];
    env_on_        = active_->DefaultEnvOn();

    hw_.SetAudioBlockSize(active_->BlockSize());
    active_->Init(hw_, hw_.AudioSampleRate());

    hw_.StartAudio(AudioThunk);

    uint32_t next_draw = 0;
    while(true)
    {
        const uint32_t now = System::GetNow();
        if((int32_t)(now - next_draw) >= 0)
        {
            DrawLegend();
            next_draw = now + 50; // ~20 fps; bit-banged I2C is slow
        }
        System::Delay(2);
    }
}

} // namespace multiosc
