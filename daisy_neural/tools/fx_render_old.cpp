// The OLD MultiFX effects (common/multifx_core, as daisy_multifx_oled ran them), rendered on
// the Mac for an A/B against Mirth's rewrite (FX.md). Same raw interface as fx_render:
//   fx_render_old <effect 0-7> <knob2> <knob3> <knob4> < in.f32 > wet.f32
// Effects 0-3 are the reverb bank's CLASSIC, PLATE, TANK, SHIMMER; 4-7 the delay bank's PING,
// TAPE, MULTITAP, ECHOVERB, each with the knobs as MultiFX had them (its p1-p3).
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "multifx_core/reverb_bank.h"
#include "multifx_core/delay_bank.h"
static mfx::ReverbBank rv;
static mfx::DelayBank  dl;
int main(int argc, char** argv)
{
    if(argc < 5) return 2;
    const int e = atoi(argv[1]);
    const float p1 = atof(argv[2]), p2 = atof(argv[3]), p3 = atof(argv[4]);
    std::vector<float> in; float buf[4096]; size_t got;
    while((got = fread(buf, sizeof(float), 4096, stdin)) > 0) in.insert(in.end(), buf, buf + got);
    const size_t frames = in.size() / 2;
    rv.Init(48000.f); dl.Init(48000.f);
    if(e < 4) rv.Reset((mfx::ReverbMode)e); else dl.Reset((mfx::DelayMode)(e - 4));
    std::vector<float> out(frames * 2);
    for(size_t f = 0; f < frames; f++)
    {
        float wl, wr;
        if(e < 4) rv.Process((mfx::ReverbMode)e, in[2*f], in[2*f+1], p1, p2, p3, wl, wr);
        else      dl.Process((mfx::DelayMode)(e - 4), in[2*f], in[2*f+1], p1, p2, p3, false, 0.f, wl, wr);
        out[2*f] = wl; out[2*f+1] = wr;
    }
    fwrite(out.data(), sizeof(float), out.size(), stdout);
}
