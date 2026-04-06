#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include <cmath>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

// ================================================================
// Constants
// ================================================================
static constexpr uint8_t kNumPatches      = 8;
static constexpr size_t  kMaxDelaySamples = 96000; // 2.0 s @ 48 k
static constexpr size_t  kGrainBufSize    = 24000; // ~0.5 s @ 48 k
static constexpr float   kLedVoltsOn      = 2.0f;
static constexpr float   kTwoPi           = 6.2831853f;

// ================================================================
// Shared hardware
// ================================================================
DaisyPatchSM patch;
ReverbSc     reverb;
using daisysp::DelayLine;
using daisysp::Svf;
DSY_SDRAM_BSS static DelayLine<float, kMaxDelaySamples> delayL;
DSY_SDRAM_BSS static DelayLine<float, kMaxDelaySamples> delayR;

// ================================================================
// Case 1 – Resonator (2-partial SVF bandpass)
// ================================================================
static Svf   bp1L, bp2L, bp1R, bp2R;
static float excite_envL = 0.0f, excite_envR = 0.0f;

// ================================================================
// Case 2 – Delay → Reverb
// (State lifted from static locals inside switch to file scope)
// ================================================================
static float fb_lp_l2  = 0.0f, fb_lp_r2  = 0.0f;
static float d2_target = 24000.0f, d2_smooth = 24000.0f;

// ================================================================
// Case 3 – Granular pitch shifter (dual-grain overlap-add)
// ================================================================
DSY_SDRAM_BSS static float g_bufferL[kGrainBufSize];
DSY_SDRAM_BSS static float g_bufferR[kGrainBufSize];
static size_t   g_bufWrite   = 0;
static float    g_grainPhase = 0.0f;
static float    g_grainRate  = 1.0f;
static float    g_grainSizeS = 0.06f;
static float    g_density    = 0.6f;
static uint32_t g_rng_state  = 0x12345678u;

// ================================================================
// Case 4 – Moog ladder filter
// ================================================================
static LadderFilter ladder_l, ladder_r;

// ================================================================
// Case 5 – SVF multi-mode morph
// ================================================================
static Svf svf_l, svf_r;

// ================================================================
// Case 6 – Comb filter / Karplus-Strong (reuses delayL/R)
// ================================================================
static float comb_lp_l = 0.0f, comb_lp_r = 0.0f;

// ================================================================
// Case 7 – Wavefolder + stereo Chorus
// ================================================================
static Wavefolder wfold_l, wfold_r;
static Chorus     chorus;

// ================================================================
// External clock detection on CV_5 (used by case 2)
// ================================================================
static bool     g_clk_gate       = false;
static uint32_t g_clk_last_ticks = 0;
static float    g_clk_interval_s = 0.0f;

// ================================================================
// UI state
// ================================================================
static Switch   s_mode_btn; // B7 – momentary, cycles patches
static Switch   s_shift_sw; // B8 – toggle, alt parameter layer
static uint8_t  g_patch_index = 0;
static bool     g_edit_focus  = false;
static uint32_t g_led_ctr     = 0;

static inline void SetPanelLed(bool on)
{
    patch.WriteCvOut(CV_OUT_2, on ? kLedVoltsOn : 0.0f);
}

static inline bool LedPulseState(uint8_t count, float t)
{
    // Short bursts: count pulses per cycle, gap between cycles for readability.
    // kCycleLen must exceed count * (kPulseOn + kPulseGap); 3.5 s covers 8 patches.
    constexpr float kCycleLen = 3.5f;
    constexpr float kPulseOn  = 0.18f;
    constexpr float kPulseGap = 0.14f;
    float cyc = fmodf(t, kCycleLen);
    for(uint8_t i = 0; i < count; i++)
    {
        float s = i * (kPulseOn + kPulseGap);
        if(cyc >= s && cyc < s + kPulseOn)
            return true;
    }
    return false;
}

static void InitUiInline(float sample_rate)
{
    s_mode_btn.Init(patch.B7, sample_rate,
                    Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED);
    s_shift_sw.Init(patch.B8, sample_rate,
                    Switch::TYPE_TOGGLE,    Switch::POLARITY_NORMAL);
    patch.StartDac();
    patch.StartAdc();
}

static void UpdateUiInline(size_t block_size)
{
    s_mode_btn.Debounce();
    s_shift_sw.Debounce();
    if(s_mode_btn.RisingEdge())
        g_patch_index = (uint8_t)((g_patch_index + 1) % kNumPatches);
    g_edit_focus = s_shift_sw.Pressed();
    g_led_ctr += block_size;
    float t = (float)g_led_ctr / patch.AudioSampleRate();
    SetPanelLed(g_edit_focus
                    ? true
                    : LedPulseState((uint8_t)(g_patch_index + 1), t));
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    patch.ProcessAllControls();
    UpdateUiInline(size);

    float k1   = patch.GetAdcValue(CV_1);
    float k2   = patch.GetAdcValue(CV_2);
    float k3   = patch.GetAdcValue(CV_3);
    float k4   = patch.GetAdcValue(CV_4);
    float kclk = patch.GetAdcValue(CV_5);

    // External clock edge detect with hysteresis (used by case 2)
    const float  hi_thr    = 0.65f;
    const float  lo_thr    = 0.35f;
    uint32_t     now_ticks = System::GetNow(); // ms since boot
    if(!g_clk_gate && kclk >= hi_thr)
    {
        g_clk_gate = true;
        if(g_clk_last_ticks != 0)
        {
            uint32_t dt_ms = now_ticks - g_clk_last_ticks;
            if(dt_ms > 0)
                g_clk_interval_s = (float)dt_ms * 0.001f;
        }
        g_clk_last_ticks = now_ticks;
    }
    else if(g_clk_gate && kclk <= lo_thr)
    {
        g_clk_gate = false;
    }

    switch(g_patch_index)
    {
        // ============================================================
        case 0: // Reverb (send topology)
        // CV1=decay  CV2=damp  CV3=dry level  CV4=send level
        // ============================================================
        {
            reverb.SetFeedback(fmap(k1, 0.3f, 0.99f));
            reverb.SetLpFreq(fmap(k2, 1000.f, 19000.f, Mapping::LOG));
            float in_level   = k3;
            float send_level = k4;
            for(size_t i = 0; i < size; i++)
            {
                float wetl, wetr;
                reverb.Process(IN_L[i] * send_level, IN_R[i] * send_level,
                               &wetl, &wetr);
                OUT_L[i] = IN_L[i] * in_level + wetl;
                OUT_R[i] = IN_R[i] * in_level + wetr;
            }
        }
        break;

        // ============================================================
        case 1: // Resonator (2-partial SVF bandpass)
        // CV1=freq  CV2=damping+brightness  CV3=in level  CV4=wet
        // ============================================================
        {
            float base_freq = fmap(k1, 60.0f, 1200.0f, Mapping::LOG);
            float damping   = fmap(k2, 0.2f, 0.95f);
            float brighten  = fmap(k2, 0.2f, 1.0f); // coupled: brighter = less damp
            float in_level  = k3;
            float wet_mix   = k4;

            // Precompute weights outside the sample loop
            const float w1 = 0.6f * (1.0f - 0.7f * brighten);
            const float w2 = 0.6f * (0.3f + 0.7f * brighten);

            bp1L.SetFreq(base_freq);        bp1L.SetRes(damping);
            bp1R.SetFreq(base_freq);        bp1R.SetRes(damping);
            bp2L.SetFreq(base_freq * 1.5f); bp2L.SetRes(damping * 0.9f);
            bp2R.SetFreq(base_freq * 1.5f); bp2R.SetRes(damping * 0.9f);

            const float excite_att = 0.002f;
            const float excite_rel = 0.0008f;

            for(size_t i = 0; i < size; i++)
            {
                float inl  = IN_L[i] * in_level;
                float inr  = IN_R[i] * in_level;
                float magL = fabsf(inl), magR = fabsf(inr);
                excite_envL += (magL - excite_envL) * (magL > excite_envL ? excite_att : excite_rel);
                excite_envR += (magR - excite_envR) * (magR > excite_envR ? excite_att : excite_rel);
                float exciteL = inl * (1.0f + excite_envL);
                float exciteR = inr * (1.0f + excite_envR);
                bp1L.Process(exciteL); bp2L.Process(exciteL);
                bp1R.Process(exciteR); bp2R.Process(exciteR);
                float wetL = bp1L.Band() * w1 + bp2L.Band() * w2;
                float wetR = bp1R.Band() * w1 + bp2R.Band() * w2;
                OUT_L[i] = inl * (1.0f - wet_mix) + wetL * wet_mix;
                OUT_R[i] = inr * (1.0f - wet_mix) + wetR * wet_mix;
            }
        }
        break;

        // ============================================================
        case 2: // Delay → Reverb
        // B8 OFF: CV1=delay time  CV2=feedback  CV3=dry  CV4=wet
        // B8 ON:  CV1=reverb decay  CV2=reverb damp  (delay uses defaults)
        // CV5 jack: external clock syncs delay time when signal present < 2 s ago
        // ============================================================
        {
            float in_level   = k3;
            float send_level = k4;

            const float kDefDelayTime = 0.200f; // 200 ms
            const float kDefDelayFb   = 0.45f;
            const float kDefRvTime    = 0.6f;
            const float kDefRvDamp    = 8000.0f;

            bool  have_clk    = (g_clk_interval_s > 0.0f)
                             && (now_ticks - g_clk_last_ticks < 2000u);
            float d_time_sec  = g_edit_focus
                ? kDefDelayTime
                : (have_clk ? g_clk_interval_s : fmap(k1, 0.02f, 2.0f));
            float d_feedback  = g_edit_focus ? kDefDelayFb : fmap(k2, 0.0f, 0.85f);
            float sr          = patch.AudioSampleRate();

            d2_target  = fminf(d_time_sec * sr, (float)kMaxDelaySamples - 1.0f);
            d2_smooth += 0.0015f * (d2_target - d2_smooth);
            delayL.SetDelay(d2_smooth);
            delayR.SetDelay(d2_smooth);

            reverb.SetFeedback(g_edit_focus ? fmap(k1, 0.3f, 0.99f) : kDefRvTime);
            reverb.SetLpFreq(g_edit_focus
                                 ? fmap(k2, 1000.f, 19000.f, Mapping::LOG)
                                 : kDefRvDamp);

            // LP on feedback path: alpha 0.55 ≈ 4.2 kHz — bright enough to retain
            // delay character while still rolling off harsh highs in long tails.
            const float fb_alpha2 = 0.55f;
            for(size_t i = 0; i < size; i++)
            {
                float dl_prev_l = delayL.Read();
                float dl_prev_r = delayR.Read();
                fb_lp_l2 += fb_alpha2 * (dl_prev_l - fb_lp_l2);
                fb_lp_r2 += fb_alpha2 * (dl_prev_r - fb_lp_r2);
                delayL.Write(IN_L[i] + fb_lp_l2 * d_feedback);
                delayR.Write(IN_R[i] + fb_lp_r2 * d_feedback);

                float wetl, wetr;
                reverb.Process(dl_prev_l, dl_prev_r, &wetl, &wetr);
                float wet_mix = send_level;
                OUT_L[i] = IN_L[i] * in_level * (1.0f - wet_mix) + wetl * wet_mix;
                OUT_R[i] = IN_R[i] * in_level * (1.0f - wet_mix) + wetr * wet_mix;
            }
        }
        break;

        // ============================================================
        case 3: // Granular pitch shifter – dual-grain overlap-add
        // CV1=pitch (±12 st)  CV2=grain size  CV3=density  CV4=wet
        //
        // Two Hann-windowed grains at 50 % phase offset. Their windows
        // sum to exactly 1.0 at all times (overlap-add property), so
        // there is no amplitude modulation artifact.
        // ============================================================
        {
            float pitch_st  = fmap(k1, -12.0f, 12.0f);
            g_grainRate     = powf(2.0f, pitch_st / 12.0f);
            g_grainSizeS    = fmap(k2, 0.025f, 0.150f);
            g_density       = fmap(k3, 0.40f, 0.85f); // independent of grain size
            float wet_mix   = k4;
            float sr        = patch.AudioSampleRate();

            size_t grainLen = (size_t)fminf(g_grainSizeS * sr,
                                            (float)kGrainBufSize - 4.0f);
            if(grainLen < 64) grainLen = 64;
            float fGrainLen = (float)grainLen;

            for(size_t i = 0; i < size; i++)
            {
                // Write stereo input to circular buffer
                g_bufferL[g_bufWrite] = IN_L[i];
                g_bufferR[g_bufWrite] = IN_R[i];

                // Grain 1 and grain 2 at half-grain phase offset
                float phase1 = g_grainPhase;
                float phase2 = fmodf(phase1 + fGrainLen * 0.5f, fGrainLen);

                // Read position in buffer for each grain (linear interp)
                auto readGrain = [&](float phase, float& outL, float& outR) {
                    float pos = fmodf((float)g_bufWrite - phase
                                     + (float)kGrainBufSize,
                                     (float)kGrainBufSize);
                    size_t i0 = (size_t)pos;
                    size_t i1 = (i0 + 1) % kGrainBufSize;
                    float  fr = pos - (float)i0;
                    outL = g_bufferL[i0] * (1.0f - fr) + g_bufferL[i1] * fr;
                    outR = g_bufferR[i0] * (1.0f - fr) + g_bufferR[i1] * fr;
                };

                float gl1, gr1, gl2, gr2;
                readGrain(phase1, gl1, gr1);
                readGrain(phase2, gl2, gr2);

                // Hann windows: sum to exactly 1.0 at 50 % overlap
                float w1 = 0.5f * (1.0f - cosf(kTwoPi * phase1 / fGrainLen));
                float w2 = 0.5f * (1.0f - cosf(kTwoPi * phase2 / fGrainLen));

                float wetl = gl1 * w1 + gl2 * w2;
                float wetr = gr1 * w1 + gr2 * w2;

                OUT_L[i] = IN_L[i] * (1.0f - wet_mix) + wetl * wet_mix;
                OUT_R[i] = IN_R[i] * (1.0f - wet_mix) + wetr * wet_mix;

                // Advance phase; probabilistic respawn controls grain density
                g_grainPhase += g_grainRate;
                if(g_grainPhase >= fGrainLen)
                {
                    g_rng_state = 1664525u * g_rng_state + 1013904223u;
                    float r01 = (float)(g_rng_state & 0x00FFFFFFu)
                                / (float)0x01000000u;
                    float respawn = r01 * g_density * fGrainLen;
                    g_grainPhase  = (respawn >= fGrainLen) ? 0.0f : respawn;
                }

                g_bufWrite = (g_bufWrite + 1) % kGrainBufSize;
            }
        }
        break;

        // ============================================================
        case 4: // Moog ladder filter (Huovilainen model, 4× oversampled)
        // CV1=cutoff (20–18k Hz LOG)  CV2=resonance (0–1.8, self-oscillates)
        // CV3=input drive (0–4)       CV4=wet/dry
        // B8 OFF: LP24    B8 ON: HP24
        // ============================================================
        {
            float cutoff = fmap(k1, 20.0f, 18000.0f, Mapping::LOG);
            float res    = fmap(k2, 0.0f, 1.8f);
            float drive  = fmap(k3, 0.0f, 4.0f);
            float wet    = k4;
            LadderFilter::FilterMode mode = g_edit_focus
                ? LadderFilter::FilterMode::HP24
                : LadderFilter::FilterMode::LP24;
            ladder_l.SetFreq(cutoff); ladder_l.SetRes(res);
            ladder_l.SetInputDrive(drive); ladder_l.SetFilterMode(mode);
            ladder_r.SetFreq(cutoff); ladder_r.SetRes(res);
            ladder_r.SetInputDrive(drive); ladder_r.SetFilterMode(mode);
            for(size_t i = 0; i < size; i++)
            {
                float fl = ladder_l.Process(IN_L[i]);
                float fr = ladder_r.Process(IN_R[i]);
                OUT_L[i] = IN_L[i] * (1.0f - wet) + fl * wet;
                OUT_R[i] = IN_R[i] * (1.0f - wet) + fr * wet;
            }
        }
        break;

        // ============================================================
        case 5: // SVF multi-mode morph (LP → BP → HP → Notch)
        // CV1=cutoff (20–16k LOG)  CV2=resonance (0–1)
        // CV3=morph (0=LP 0.33=BP 0.67=HP 1=Notch)  CV4=wet
        // B8 ON: boosts drive for gritty character
        // ============================================================
        {
            float cutoff = fmap(k1, 20.0f, 16000.0f, Mapping::LOG);
            float res    = fmap(k2, 0.0f, 1.0f);
            float morph  = k3;
            float wet    = k4;
            float drive  = g_edit_focus ? 2.5f : 1.0f;
            svf_l.SetFreq(cutoff); svf_l.SetRes(res); svf_l.SetDrive(drive);
            svf_r.SetFreq(cutoff); svf_r.SetRes(res); svf_r.SetDrive(drive);
            for(size_t i = 0; i < size; i++)
            {
                svf_l.Process(IN_L[i]);
                svf_r.Process(IN_R[i]);
                // 4-zone crossfade across LP / BP / HP / Notch outputs
                float fl, fr;
                if(morph < 0.333f)
                {
                    float t = morph / 0.333f;
                    fl = svf_l.Low()  * (1.0f - t) + svf_l.Band()  * t;
                    fr = svf_r.Low()  * (1.0f - t) + svf_r.Band()  * t;
                }
                else if(morph < 0.667f)
                {
                    float t = (morph - 0.333f) / 0.334f;
                    fl = svf_l.Band() * (1.0f - t) + svf_l.High()  * t;
                    fr = svf_r.Band() * (1.0f - t) + svf_r.High()  * t;
                }
                else
                {
                    float t = (morph - 0.667f) / 0.333f;
                    fl = svf_l.High() * (1.0f - t) + svf_l.Notch() * t;
                    fr = svf_r.High() * (1.0f - t) + svf_r.Notch() * t;
                }
                OUT_L[i] = IN_L[i] * (1.0f - wet) + fl * wet;
                OUT_R[i] = IN_R[i] * (1.0f - wet) + fr * wet;
            }
        }
        break;

        // ============================================================
        case 6: // Comb filter / Karplus-Strong
        // CV1=frequency (40–1200 Hz LOG) → sets delay length
        // CV2=feedback (0–0.99)   CV3=brightness (LP cutoff in loop)
        // CV4=wet/dry
        // B8 ON: soft-saturate feedback path for harmonic enrichment
        // ============================================================
        {
            float freq     = fmap(k1, 40.0f, 1200.0f, Mapping::LOG);
            float feedback = fmap(k2, 0.0f, 0.99f);
            float bright   = k3; // 0=dark (tight LP), 1=bright (open LP)
            float wet      = k4;
            float sr       = patch.AudioSampleRate();
            float delay_samp = fminf(sr / freq, (float)kMaxDelaySamples - 1.0f);
            delayL.SetDelay(delay_samp);
            delayR.SetDelay(delay_samp);
            // One-pole LP in feedback loop shapes the comb tone
            // alpha 0.08 (very dark) → 0.92 (near-transparent)
            float lp_alpha = fmap(bright, 0.08f, 0.92f);
            for(size_t i = 0; i < size; i++)
            {
                float dl = delayL.Read();
                float dr = delayR.Read();
                comb_lp_l += lp_alpha * (dl - comb_lp_l);
                comb_lp_r += lp_alpha * (dr - comb_lp_r);
                float fb_l = comb_lp_l * feedback;
                float fb_r = comb_lp_r * feedback;
                if(g_edit_focus) // soft saturation adds odd harmonics
                {
                    fb_l = fb_l / (1.0f + fabsf(fb_l));
                    fb_r = fb_r / (1.0f + fabsf(fb_r));
                }
                delayL.Write(IN_L[i] + fb_l);
                delayR.Write(IN_R[i] + fb_r);
                OUT_L[i] = IN_L[i] * (1.0f - wet) + dl * wet;
                OUT_R[i] = IN_R[i] * (1.0f - wet) + dr * wet;
            }
        }
        break;

        // ============================================================
        case 7: // Wavefolder + stereo Chorus
        // CV1=fold gain (1–8)   CV2=chorus rate (0.1–5 Hz)
        // CV3=chorus depth      CV4=wet/dry
        // B8 ON: bypass chorus (wavefolder only)
        // ============================================================
        {
            float fold_gain    = fmap(k1, 1.0f, 8.0f);
            float chorus_rate  = fmap(k2, 0.1f, 5.0f);
            float chorus_depth = k3;
            float wet          = k4;
            wfold_l.SetGain(fold_gain);
            wfold_r.SetGain(fold_gain);
            if(!g_edit_focus)
            {
                chorus.SetLfoFreq(chorus_rate);
                chorus.SetLfoDepth(chorus_depth);
            }
            for(size_t i = 0; i < size; i++)
            {
                float fl = wfold_l.Process(IN_L[i]);
                float fr = wfold_r.Process(IN_R[i]);
                if(!g_edit_focus)
                {
                    // Chorus takes a mono sum; left/right engines diverge via LFO offset
                    chorus.Process((fl + fr) * 0.5f);
                    fl = chorus.GetLeft();
                    fr = chorus.GetRight();
                }
                OUT_L[i] = IN_L[i] * (1.0f - wet) + fl * wet;
                OUT_R[i] = IN_R[i] * (1.0f - wet) + fr * wet;
            }
        }
        break;

        default:
        {
            for(size_t i = 0; i < size; i++)
            {
                OUT_L[i] = IN_L[i];
                OUT_R[i] = IN_R[i];
            }
        }
        break;
    }
}

int main(void)
{
    patch.Init();

    float sr = patch.AudioSampleRate();
    reverb.Init(sr);
    delayL.Init();
    delayR.Init();

    // Case 1 – Resonator
    bp1L.Init(sr); bp2L.Init(sr);
    bp1R.Init(sr); bp2R.Init(sr);

    // Case 4 – Ladder filter
    ladder_l.Init(sr);
    ladder_r.Init(sr);

    // Case 5 – SVF morph
    svf_l.Init(sr);
    svf_r.Init(sr);

    // Case 7 – Wavefolder + Chorus
    wfold_l.Init();
    wfold_r.Init();
    chorus.Init(sr);
    chorus.SetDelay(0.4f);  // ~20 ms centre delay for warm chorus character
    chorus.SetFeedback(0.1f);

    InitUiInline(sr);
    patch.StartAudio(AudioCallback);
    while(1) {}
}
