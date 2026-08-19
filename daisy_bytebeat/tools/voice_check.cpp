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
    if (rms) { double a=0; for(int i=0;i<48;i++) a+=bufL[i]*bufL[i]; *rms=std::sqrt(a/48); }
}

int main() {
    v.Init(hw, 48000.f);
    printf("DefaultEnvOn = %d (expect 0)\n", (int)v.DefaultEnvOn());
    printf("Name=%s ShortLabel=%s\n", v.Name(), v.ShortLabel());
    printf("Play labels : %s %s %s\n", v.ModLabel(0,false), v.ModLabel(1,false), v.ModLabel(2,false));
    printf("Edit labels : %s %s %s\n\n", v.ModLabel(0,true), v.ModLabel(1,true), v.ModLabel(2,true));

    EngineContext play;                 // Play page, no gestures
    double rms;

    hw.adc[3] = 0.0f;                   // MOD3 -> slot 0 of the current bank
    block(play, &rms);
    printf("MOD3=0.00  -> %-22s rms=%.4f\n", v.Selection(), rms);
    hw.adc[3] = 0.5f;
    block(play, &rms);
    printf("MOD3=0.50  -> %-22s rms=%.4f\n", v.Selection(), rms);
    hw.adc[3] = 0.99f;
    block(play, &rms);
    printf("MOD3=0.99  -> %-22s rms=%.4f\n", v.Selection(), rms);

    printf("\nShort press cycles the bank (MOD3 held at 0.99 = last of each):\n");
    for (int i = 0; i < 6; i++) {
        EngineContext sp; sp.short_press = true;
        block(sp);
        block(play, &rms);
        printf("  press %d -> %-22s rms=%.4f\n", i+1, v.Selection(), rms);
    }

    printf("\nPointer stability (host compares Selection() by pointer):\n");
    hw.adc[3]=0.0f; block(play); const char* p1 = v.Selection();
    block(play);                        const char* p2 = v.Selection();
    hw.adc[3]=0.5f; block(play);        const char* p3 = v.Selection();
    printf("  same formula: %s   changed formula: %s\n",
           p1==p2 ? "stable" : "CHANGES (bug)", p1!=p3 ? "differs" : "SAME (bug)");

    printf("\nEdit page soft-takeover on Func2 (knob parked away from the value):\n");
    EngineContext ed; ed.edit_page = true; ed.page_changed = true;
    hw.adc[1] = 0.9f;                   // MOD1 far from the stored func2 (~0.01)
    block(ed);
    ed.page_changed = false;
    for (int i = 0; i < 3; i++) block(ed);
    printf("  after 4 edit blocks with MOD1 parked at 0.90: Func1 still %s\n", v.Selection());
    printf("  (Func2 must NOT have jumped; it is caught only on sweep-through)\n");

    printf("\nRate mapping (TUNE, V/OCT at 0):\n");
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        hw.adc[0] = t; hw.adc[3] = 0.0f;
        block(play);
        printf("  TUNE=%.2f -> %.4gx\n", t, std::pow(2.0, (t-0.5)*12.0));
    }

    // Formula 85 "Gently Evolving" sits at a raw mean near -0.5: without the
    // blocker that is half of full scale of DC at the jack. Four presses from
    // TEXT reach MELOD; slot 5 of that family is index 85. Let the one-pole
    // settle (~7.6 Hz corner) before measuring, or the transient dominates.
    printf("\nDC blocker on the worst offender:\n");
    v.Init(hw, 48000.f);
    hw.adc[0]=0.5f; hw.adc[1]=0.5f; hw.adc[2]=0.5f;
    for (int i=0;i<4;i++) { EngineContext sp; sp.short_press=true; block(sp); }
    hw.adc[3]=0.25f; block(play);
    printf("  selected %s (expect Gently Evolving)\n", v.Selection());
    for (int b=0;b<100;b++) block(play);
    double mean=0; int n=0;
    for (int b=0;b<2000;b++){ block(play); for(int i=0;i<48;i++){ mean+=bufL[i]; n++; } }
    printf("  output mean over %.1f s = %+.5f (raw engine mean is -0.500)\n", n/48000.0, mean/n);

    EngineContext g; hw.gate_in_1.pending = true; block(g);
    printf("\nSync via gate_in_1.Trig(): accepted without fault\n");
    return 0;
}
