// Control-map checks for BytebeatVoice, driven through the stub hardware in
// stub/. Covers what the host-side engine checks cannot: the Play/Edit mapping,
// formula stepping, Selection() pointer stability, Tone on the Play page, and
// the output DC blocker.
#include "bytebeat_voice.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace multiosc;
using daisy::patch_sm::DaisyPatchSM;

static DaisyPatchSM  hw;
static BytebeatVoice v;
static float bufL[48], bufR[48];

static void block(EngineContext ctx, double* rms = nullptr) {
    float* o[2] = {bufL, bufR};
    v.Process(hw, ctx, nullptr, o, 48);
    if (rms) { double a = 0; for (int i = 0; i < 48; i++) a += bufL[i] * bufL[i]; *rms = std::sqrt(a / 48); }
}
static void neutral() { hw.adc[0] = 0.5f; hw.adc[1] = 0.5f; hw.adc[2] = 0.5f; hw.adc[3] = 0.5f;
                        for (int i = 4; i < 8; i++) hw.adc[i] = 0.f; }

int main() {
    v.Init(hw, 48000.f); neutral();
    EngineContext play;
    double rms;

    printf("Name=%s  ShortLabel=%s  DefaultEnvOn=%d (expect 0)\n",
           v.Name(), v.ShortLabel(), (int)v.DefaultEnvOn());
    printf("Play labels : %s %s %s   (expect A B Tone)\n",
           v.ModLabel(0,false), v.ModLabel(1,false), v.ModLabel(2,false));
    printf("Edit labels : %s %s %s   (expect Bank Func2 Drone)\n\n",
           v.ModLabel(0,true), v.ModLabel(1,true), v.ModLabel(2,true));

    printf("Short press steps within the family and wraps at its end:\n");
    block(play);
    printf("  start    -> %s\n", v.Selection());
    for (int i = 0; i < 4; i++) { EngineContext sp; sp.short_press = true; block(sp); block(play, &rms);
        printf("  press %-2d -> %-22s rms=%.4f\n", i+1, v.Selection(), rms); }
    for (int i = 0; i < 16; i++) { EngineContext sp; sp.short_press = true; block(sp); }
    block(play);
    printf("  +16 more -> %-22s (20 presses = one lap of the family)\n", v.Selection());

    printf("\nEdit MOD1 selects the family (sweeping through the takeover):\n");
    for (float k : {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 0.99f}) {
        v.Init(hw, 48000.f); neutral();
        EngineContext ed; ed.edit_page = true; ed.page_changed = true; block(ed);
        ed.page_changed = false;
        // Soft-takeover catches only on sweep-through, so sweep MOD1 down to
        // the stored value and back up, as a hand would.
        for (float x = 0.5f; x > -0.001f; x -= 0.01f) { hw.adc[1] = x < 0.f ? 0.f : x; block(ed); }
        for (float x = 0.0f; x < k + 0.001f; x += 0.01f) { hw.adc[1] = x; block(ed); }
        printf("  MOD1=%.2f -> %s\n", k, v.Selection());
    }

    v.Init(hw, 48000.f); neutral();
    printf("\nSelection() pointer stability (the host's redraw compares pointers):\n");
    block(play); const char* p1 = v.Selection();
    block(play); const char* p2 = v.Selection();
    { EngineContext sp; sp.short_press = true; block(sp); }
    block(play); const char* p3 = v.Selection();
    printf("  unchanged: %s   after a press: %s\n",
           p1 == p2 ? "stable" : "CHANGES (bug)", p1 != p3 ? "differs" : "SAME (bug)");

    printf("\nTone on Play MOD3 — clean at noon, coloured either side:\n");
    for (float k : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        v.Init(hw, 48000.f); neutral();
        hw.adc[3] = k;
        for (int b = 0; b < 40; b++) block(play);      // let the filters settle
        double d = 0, peak = 0; float prev = bufL[0];
        for (int b = 0; b < 200; b++) { block(play);
            for (int i = 0; i < 48; i++) { d += std::fabs(bufL[i] - prev); prev = bufL[i];
                                           if (std::fabs(bufL[i]) > peak) peak = std::fabs(bufL[i]); } }
        printf("  MOD3=%.2f  brightness=%.5f  peak=%.4f%s\n",
               k, d / (200 * 48), peak, k == 0.5f ? "  <- deadzone, true bypass" : "");
    }

    // Formula 85 "Gently Evolving" sits at a raw mean of -0.5: without the
    // blocker that is half of full scale of DC at the jack. Reach MELOD, then
    // let the one-pole settle (~7.6 Hz corner) before measuring.
    printf("\nDC blocker on the worst offender:\n");
    v.Init(hw, 48000.f); neutral();
    { EngineContext ed; ed.edit_page = true; ed.page_changed = true; block(ed);
      ed.page_changed = false;
      for (float x = 0.5f; x > -0.001f; x -= 0.01f) { hw.adc[1] = x < 0.f ? 0.f : x; block(ed); }
      for (float x = 0.0f; x < 0.81f; x += 0.01f) { hw.adc[1] = x; block(ed); } }
    neutral(); block(play);
    for (int i = 0; i < 5; i++) { EngineContext sp; sp.short_press = true; block(sp); }
    block(play);
    printf("  selected %s (expect Gently Evolving)\n", v.Selection());
    for (int b = 0; b < 100; b++) block(play);
    double mean = 0; int n = 0;
    for (int b = 0; b < 2000; b++) { block(play); for (int i = 0; i < 48; i++) { mean += bufL[i]; n++; } }
    printf("  output mean over %.1f s = %+.5f (raw engine mean is -0.500)\n", n / 48000.0, mean / n);

    EngineContext g; hw.gate_in_1.pending = true; block(g);
    printf("\nSync via gate_in_1.Trig(): accepted without fault\n");
    return 0;
}
