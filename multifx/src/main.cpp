#include "daisy_patch_sm.h"
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include <cmath>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;

DaisyPatchSM patch;
ReverbSc     reverb;
// Simple stereo delay lines for Patch 1
using daisysp::DelayLine;
using daisysp::Svf;
// Extended delay using SDRAM: target up to ~2.0s at 48kHz per channel
static constexpr size_t kMaxDelaySamples = 96000; // 2.0s @ 48k
DSY_SDRAM_BSS static DelayLine<float, kMaxDelaySamples> delayL;
DSY_SDRAM_BSS static DelayLine<float, kMaxDelaySamples> delayR;
// Simple modal resonator filters
static Svf bp1L, bp2L, bp1R, bp2R;
static float excite_envL = 0.0f, excite_envR = 0.0f;

// Inline UI state and handling modeled after working patches
static Switch s_mode_btn; // B7
static Switch s_shift_sw; // B8
static uint8_t g_patch_index = 0;
static bool    g_edit_focus  = false;
static uint32_t g_led_ctr    = 0;
static constexpr float kLedVoltsOn = 2.0f; // slightly brighter for visibility via CV_OUT_2
// Granular pitch shifter buffers
DSY_SDRAM_BSS static float g_bufferL[24000]; // ~0.5s at 48k across both if windowed
DSY_SDRAM_BSS static float g_bufferR[24000];
static size_t g_bufWrite = 0;
static float  g_grainPhase = 0.0f;
static float  g_grainRate  = 1.0f; // playback rate
static float  g_grainSizeS = 0.06f; // seconds
static float  g_density    = 0.6f;  // overlap factor 0..1
static uint32_t g_rng_state = 0x12345678u;
// External clock detection (CV_5) for delay sync
static float   g_clk_prev    = 0.0f;
static bool    g_clk_gate    = false;
static uint32_t g_clk_last_ticks = 0;
static float   g_clk_interval_s = 0.0f; // seconds between rising edges

static inline void SetPanelLed(bool on)
{
    patch.WriteCvOut(CV_OUT_2, on ? kLedVoltsOn : 0.0f);
}

static inline bool LedPulseState(uint8_t count, float t)
{
    // FM4Op-style pulse train: short pulses separated by gaps, repeated cyclically
    constexpr float kCycleLen   = 2.5f;   // seconds per cycle
    constexpr float kPulseOn    = 0.20f;  // seconds LED on per pulse (more visible)
    constexpr float kPulseGap   = 0.15f;  // seconds between pulses
    float cyc = fmodf(t, kCycleLen);
    for(uint8_t i = 0; i < count; i++)
    {
        float s = i * (kPulseOn + kPulseGap);
        float e = s + kPulseOn;
        if(cyc >= s && cyc < e)
            return true;
    }
    return false;
}

static void InitUiInline(float sample_rate)
{
    s_mode_btn.Init(patch.B7,
                    sample_rate,
                    Switch::TYPE_MOMENTARY,
                    Switch::POLARITY_INVERTED);
    s_shift_sw.Init(patch.B8,
                    sample_rate,
                    Switch::TYPE_TOGGLE,
                    Switch::POLARITY_NORMAL);
    // Enable DAC for panel LED CV output (matches FM4Op usage)
    patch.StartDac();
    // Ensure ADC is running for switch and CV reads (consistent with FM4Op)
    patch.StartAdc();
}

static void UpdateUiInline(size_t block_size)
{
    // Debounce once per audio block
    s_mode_btn.Debounce();
    s_shift_sw.Debounce();
    if(s_mode_btn.RisingEdge())
        g_patch_index = (uint8_t)((g_patch_index + 1) % 4); // 4 patches: 0..3
    g_edit_focus = s_shift_sw.Pressed();
    // LED: steady ON when editing; else pulse per patch count
    g_led_ctr += block_size;
    float t = (float)g_led_ctr / patch.AudioSampleRate();
    if(g_edit_focus)
        SetPanelLed(true);
    else
        SetPanelLed(LedPulseState((uint8_t)(g_patch_index + 1), t));
}

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    // Process all controls (buttons, switches, CVs) like FM4Op
    patch.ProcessAllControls();

    // Update inline UI (B7/B8), mirroring other working patches
    UpdateUiInline(size);

    // Read knobs once; map per patch using edit focus (B8)
    float k1 = patch.GetAdcValue(CV_1);
    float k2 = patch.GetAdcValue(CV_2);
    float k3 = patch.GetAdcValue(CV_3);
    float k4 = patch.GetAdcValue(CV_4);
    float kclk = patch.GetAdcValue(CV_5); // external clock input (0..1 normalized)
    // Robust edge detect with hysteresis; do not assume idle level when unplugged
    const float hi_thr = 0.65f;
    const float lo_thr = 0.35f;
    uint32_t now_ticks = System::GetNow();
    if(!g_clk_gate && kclk >= hi_thr)
    {
        // Rising edge detected
        g_clk_gate = true;
        if(g_clk_last_ticks != 0)
        {
            uint32_t dt_ticks = now_ticks - g_clk_last_ticks;
            // Convert ticks to seconds using System::GetTickFreq()
            float tick_hz = (float)System::GetTickFreq();
            if(dt_ticks > 0 && tick_hz > 0.0f)
                g_clk_interval_s = (float)dt_ticks / tick_hz;
        }
        g_clk_last_ticks = now_ticks;
    }
    else if(g_clk_gate && kclk <= lo_thr)
    {
        g_clk_gate = false;
    }

    // Patch selection: 0 = Reverb, 1 = Delay, 2 = Delay→Reverb, 3 = Granular Pitch, others pass-through
    switch(g_patch_index)
    {
        case 0: // Reverb patch (send-only topology)
        {
            // Consistent mapping:
            // B8 OFF/ON: CV1=decay, CV2=damp, CV3=input level, CV4=send to reverb
            float time = fmap(k1, 0.3f, 0.99f);
            float damp = fmap(k2, 1000.f, 19000.f, Mapping::LOG);
            float in_level   = k3;
            float send_level = k4;
            reverb.SetFeedback(time);
            reverb.SetLpFreq(damp);
            for(size_t i = 0; i < size; i++)
            {
                float dryl  = IN_L[i] * in_level;
                float dryr  = IN_R[i] * in_level;
                float sendl = IN_L[i] * send_level;
                float sendr = IN_R[i] * send_level;
                float wetl, wetr;
                reverb.Process(sendl, sendr, &wetl, &wetr);
                OUT_L[i] = dryl + wetl;
                OUT_R[i] = dryr + wetr;
            }
        }
        break;

        case 1: // Resonator (Rings-inspired, license-safe)
        {
            // CV mapping: CV1 freq, CV2 damping/brightness, CV3 input level, CV4 wet/dry
            float base_freq = fmap(k1, 60.0f, 1200.0f, Mapping::LOG);
            float damping   = fmap(k2, 0.2f, 0.95f);
            // brightness as weighting between partials (darker to brighter)
            float brighten  = fmap(k2, 0.2f, 1.0f);
            float in_level  = k3;
            float wet_mix   = k4;

            bp1L.SetFreq(base_freq);
            bp1L.SetRes(damping);
            bp1R.SetFreq(base_freq);
            bp1R.SetRes(damping);
            bp2L.SetFreq(base_freq * 1.5f);
            bp2L.SetRes(damping * 0.9f);
            bp2R.SetFreq(base_freq * 1.5f);
            bp2R.SetRes(damping * 0.9f);

            const float excite_att = 0.002f;
            const float excite_rel = 0.0008f;

            for(size_t i = 0; i < size; i++)
            {
                float inl = IN_L[i] * in_level;
                float inr = IN_R[i] * in_level;
                float dryl = IN_L[i] * in_level;
                float dryr = IN_R[i] * in_level;
                float magL = fabsf(inl);
                float magR = fabsf(inr);
                float targetL = magL;
                float targetR = magR;
                excite_envL += (targetL - excite_envL) * (targetL > excite_envL ? excite_att : excite_rel);
                excite_envR += (targetR - excite_envR) * (targetR > excite_envR ? excite_att : excite_rel);
                float exciteL = inl + (inl * excite_envL);
                float exciteR = inr + (inr * excite_envR);

                bp1L.Process(exciteL);
                bp2L.Process(exciteL);
                bp1R.Process(exciteR);
                bp2R.Process(exciteR);
                // brightness shaping via weights (do not retune filter freq per-sample)
                float w1 = 0.6f * (1.0f - 0.7f * brighten); // darker reduces first partial
                float w2 = 0.6f * (0.3f + 0.7f * brighten); // brighter emphasizes second partial
                float wetL = bp1L.Band() * w1 + bp2L.Band() * w2;
                float wetR = bp1R.Band() * w1 + bp2R.Band() * w2;

                OUT_L[i] = dryl * (1.0f - wet_mix) + wetL * wet_mix;
                OUT_R[i] = dryr * (1.0f - wet_mix) + wetR * wet_mix;
            }
        }
        break;

        case 2: // Delay followed by Reverb (edit-focus switches control set)
        {
            // Consistent mapping across modes:
            // CV3=input level, CV4=send level (into delay)
            // B8 OFF: CV1=time (delay), CV2=feedback (delay). Reverb uses defaults.
            // B8 ON:  CV1=decay (reverb), CV2=damp (reverb). Delay uses defaults.
            float in_level   = k3;
            float send_level = k4;
            // Defaults when not in focus
            const float kDefDelayTime = 0.200f; // 200 ms
            const float kDefDelayFb   = 0.45f;
            const float kDefRvTime    = 0.6f;
            const float kDefRvDamp    = 8000.0f;

            // If external clock present recently (< 2s), use it as delay time when B8 is OFF
            bool have_clk = (g_clk_interval_s > 0.0f) && (System::GetNow() - g_clk_last_ticks < 2u * System::GetTickFreq());
            float clk_time_sec = have_clk ? g_clk_interval_s : 0.0f;
            float d_time_sec = g_edit_focus ? kDefDelayTime : (have_clk ? clk_time_sec : fmap(k1, 0.02f, 2.0f));
            float d_feedback = g_edit_focus ? kDefDelayFb   : fmap(k2, 0.0f, 0.85f);
            size_t sr        = (size_t)patch.AudioSampleRate();
            // Smooth delay time
            static float d2_target = 24000.0f;
            static float d2_smooth = 24000.0f;
            d2_target = fminf(d_time_sec * sr, (float)kMaxDelaySamples - 1);
            d2_smooth += 0.0015f * (d2_target - d2_smooth);
            delayL.SetDelay(d2_smooth);
            delayR.SetDelay(d2_smooth);
            float rv_time = g_edit_focus ? fmap(k1, 0.3f, 0.99f) : kDefRvTime;
            float rv_damp = g_edit_focus ? fmap(k2, 1000.f, 19000.f, Mapping::LOG) : kDefRvDamp;
            reverb.SetFeedback(rv_time);
            reverb.SetLpFreq(rv_damp);

            for(size_t i = 0; i < size; i++)
            {
                float inl = IN_L[i];
                float inr = IN_R[i];
                float dryl = inl * in_level;
                float dryr = inr * in_level;

                float dl_prev_l = delayL.Read();
                float dl_prev_r = delayR.Read();
                // Filter feedback path for stability
                static float fb_lp_l2 = 0.0f, fb_lp_r2 = 0.0f;
                const float fb_alpha2 = 0.2f;
                fb_lp_l2 = fb_lp_l2 + fb_alpha2 * (dl_prev_l - fb_lp_l2);
                fb_lp_r2 = fb_lp_r2 + fb_alpha2 * (dl_prev_r - fb_lp_r2);
                // Write: dry input plus filtered feedback
                delayL.Write(inl + fb_lp_l2 * d_feedback);
                delayR.Write(inr + fb_lp_r2 * d_feedback);

                float dl_l = dl_prev_l;
                float dl_r = dl_prev_r;
                float wetl, wetr;
                reverb.Process(dl_l, dl_r, &wetl, &wetr);
                // Mix reverb output using send as wet amount
                float wet_mix = send_level;
                OUT_L[i] = dryl * (1.0f - wet_mix) + wetl * wet_mix;
                OUT_R[i] = dryr * (1.0f - wet_mix) + wetr * wet_mix;
            }
        }
        break;

        case 3: // Granular Pitch Shifter (license-safe, lightweight)
        {
            // CV1: pitch in semitones (-12..+12), CV2: grain size/density, CV3: input level, CV4: wet/dry
            float pitch_st   = fmap(k1, -12.0f, 12.0f);
            float semitone   = powf(2.0f, pitch_st / 12.0f);
            float in_level   = k3;
            float wet_mix    = k4;
            float sr         = patch.AudioSampleRate();
            g_grainRate      = semitone;
            g_grainSizeS     = fmap(k2, 0.025f, 0.150f);
            g_density        = fmap(k2, 0.40f, 0.85f);
            size_t grainLen  = (size_t)fminf(g_grainSizeS * sr, (float)24000 - 4);
            if(grainLen < 64) grainLen = 64; // avoid tiny grains

            for(size_t i = 0; i < size; i++)
            {
                float inl = IN_L[i] * in_level;
                float inr = IN_R[i] * in_level;
                // write input to circular buffer
                g_bufferL[g_bufWrite] = inl;
                g_bufferR[g_bufWrite] = inr;

                // Hann windowed grain playback with rate
                float readPos = (float)g_bufWrite - g_grainPhase;
                readPos = fmodf(readPos, 24000.0f);
                if(readPos < 0.0f) readPos += 24000.0f;
                size_t idx0 = (size_t)readPos;
                size_t idx1 = (idx0 + 1) % 24000;
                float frac = readPos - (float)idx0;
                float gl = g_bufferL[idx0] * (1.0f - frac) + g_bufferL[idx1] * frac;
                float gr = g_bufferR[idx0] * (1.0f - frac) + g_bufferR[idx1] * frac;

                // Window based on phase within grain
                float phaseNorm = fmodf(g_grainPhase, (float)grainLen) / (float)grainLen;
                float w = 0.5f * (1.0f - cosf(2.0f * M_PI * phaseNorm));
                float wetl = gl * w;
                float wetr = gr * w;

                // advance phase; respawn grains by density
                g_grainPhase += g_grainRate;
                if(g_grainPhase >= (float)grainLen)
                {
                    // probabilistic respawn to control density using bounded RNG
                    g_rng_state = 1664525u * g_rng_state + 1013904223u;
                    float r01 = (float)(g_rng_state & 0x00FFFFFFu) / (float)0x01000000u; // 0..1
                    float respawn = r01 * g_density * (float)grainLen;
                    if(respawn >= (float)grainLen) respawn = 0.0f;
                    g_grainPhase = respawn;
                }

                // mix
                OUT_L[i] = IN_L[i] * (1.0f - wet_mix) + wetl * wet_mix;
                OUT_R[i] = IN_R[i] * (1.0f - wet_mix) + wetr * wet_mix;

                // increment write index
                g_bufWrite++;
                if(g_bufWrite >= 24000) g_bufWrite = 0;
            }
        }
        break;

        default: // Placeholder: pass-through with input level
        {
            for(size_t i = 0; i < size; i++)
            {
                float in_level = k3;
                OUT_L[i] = IN_L[i] * in_level;
                OUT_R[i] = IN_R[i] * in_level;
            }
        }
        break;
    }
}

int main(void)
{
    patch.Init();
    reverb.Init(patch.AudioSampleRate());
    delayL.Init();
    delayR.Init();
    // Init resonator filters
    bp1L.Init(patch.AudioSampleRate());
    bp2L.Init(patch.AudioSampleRate());
    bp1R.Init(patch.AudioSampleRate());
    bp2R.Init(patch.AudioSampleRate());
    InitUiInline(patch.AudioSampleRate());
    patch.StartAudio(AudioCallback);
    while(1) {}
}