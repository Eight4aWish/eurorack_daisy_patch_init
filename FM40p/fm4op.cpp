#include "daisy_patch_sm.h"
#include "daisysp.h"
#include <cmath>

using namespace daisy;
using namespace daisysp;
using namespace patch_sm;
DaisyPatchSM hw;
Switch       mode_button;
Adsr         env;
Switch       shift_switch; // B8 toggle for edit mode
float        master_gain = 0.6f; // reduce overall loudness

struct Operator {
    float phase;
    float ratio;
    float mod_index;
    float incr;
    float last_out; // for feedback or storage
};

static constexpr float kTwoPi = 2.0f * M_PI;
static constexpr float kPanelLedVoltsMax   = 4.0f;  // CV_OUT drive when PWM is ON
static constexpr float kPanelLedBrightness = 0.25f; // 0..1 duty cycle
static constexpr uint32_t kPanelLedPwmPeriodMs = 4; // ~250Hz PWM at 1ms resolution
Operator ops[4];
float    sample_rate;
uint8_t  algo_mode = 0; // 0: Parallel, 1: Serial ratios, 2: Feedback
bool     btn_prev = false; // unused after Switch adoption

// Attempt to drive panel LED via CV_OUT_2; fallback to Seed LED if unavailable
inline void SetPanelLed(bool on) {
    // Board (Seed) user LED:
    hw.SetLed(on);

    // Front-panel LED is effectively driven from a CV DAC path on some Patch SM builds.
    // Using a low analog voltage can fall below the LED’s visible threshold, so dim via PWM.
    bool pwm_on = false;
    if(on)
    {
        uint32_t on_ms = (uint32_t)(kPanelLedPwmPeriodMs * kPanelLedBrightness + 0.5f);
        if(on_ms == 0)
            on_ms = 1;
        if(on_ms > kPanelLedPwmPeriodMs)
            on_ms = kPanelLedPwmPeriodMs;

        const uint32_t now = System::GetNow();
        pwm_on             = (now % kPanelLedPwmPeriodMs) < on_ms;
    }

    const float volts = pwm_on ? kPanelLedVoltsMax : 0.0f;
    // Some panels wire the visible LED to CV_OUT_1, others to CV_OUT_2.
    hw.WriteCvOut(CV_OUT_1, volts);
    hw.WriteCvOut(CV_OUT_2, volts);
}

inline void UpdateLedPattern(uint8_t mode)
{
    static uint32_t next_ms      = 0;
    static uint8_t  pulse_index  = 0;
    static bool     led_is_on    = false;

    const uint32_t now = System::GetNow();
    if((int32_t)(now - next_ms) < 0)
        return;

    if(shift_switch.Pressed())
    {
        SetPanelLed(true);
        led_is_on = true;
        next_ms   = now + 25;
        return;
    }

    const uint8_t pulses = (mode % 3) + 1;
    constexpr uint32_t kOnMs   = 140;
    constexpr uint32_t kOffMs  = 160;
    constexpr uint32_t kPauseMs = 900;

    if(led_is_on)
    {
        SetPanelLed(false);
        led_is_on = false;
        pulse_index++;

        if(pulse_index >= pulses)
        {
            pulse_index = 0;
            next_ms     = now + kPauseMs;
        }
        else
        {
            next_ms = now + kOffMs;
        }
    }
    else
    {
        SetPanelLed(true);
        led_is_on = true;
        next_ms   = now + kOnMs;
    }
}

inline float WrapPhase(float p) {
    if(p >= kTwoPi) p -= kTwoPi;
    if(p < 0.0f) p += kTwoPi;
    return p;
}
inline float FastSin(float p) { return sinf(p); }

float KnobToBaseFreq(float k) {
    return 50.0f * powf(2.0f, k * 6.0f); // ~50Hz -> ~3.2kHz
}

void RecomputeIncrements(float base_freq) {
    for(int i = 0; i < 4; i++) {
        float f   = base_freq * ops[i].ratio;
        ops[i].incr = kTwoPi * f / sample_rate;
    }
}

void InitSynth() {
    sample_rate = hw.AudioSampleRate();
    ops[0].ratio = 1.0f;
    ops[1].ratio = 2.0f;
    ops[2].ratio = 3.0f;
    ops[3].ratio = 4.0f;
    for(int i = 0; i < 4; i++) {
        ops[i].phase     = 0.0f;
        ops[i].mod_index = 0.0f;
        ops[i].incr      = 0.0f;
        ops[i].last_out  = 0.0f;
    }
    env.Init(sample_rate);
    env.SetTime(ADSR_SEG_ATTACK, 0.01f);
    env.SetTime(ADSR_SEG_DECAY, 0.15f);
    env.SetTime(ADSR_SEG_RELEASE, 0.4f);
    env.SetSustainLevel(0.7f);
    algo_mode = 0;
    // Start CV DAC for panel LED pulses
    hw.StartDac();
    // Initialize mode_button on B7 with debounce using Switch helper
    mode_button.Init(hw.B7,
                     sample_rate,
                     Switch::TYPE_MOMENTARY,
                     Switch::POLARITY_INVERTED);
    // Initialize shift/edit toggle on B8
    shift_switch.Init(hw.B8,
                      sample_rate,
                      Switch::TYPE_TOGGLE,
                      Switch::POLARITY_NORMAL);
}

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size) {
    hw.ProcessAllControls();
    static bool gate_seen = false;
    float k0 = hw.GetAdcValue(0); // pitch
    float k1 = hw.GetAdcValue(1); // spare pot A (mapped per algo)
    float k2 = hw.GetAdcValue(2); // spare pot B (mapped per algo)
    float k3 = hw.GetAdcValue(3); // spare pot C (mapped per algo)
    float cv_pitch = hw.GetAdcValue(CV_5); // 1V/Oct input

    if(hw.gate_in_1.Trig())
        env.Retrigger(true);

    // Debounce and handle panel button
    mode_button.Debounce();
    shift_switch.Debounce();
    if(mode_button.RisingEdge()) {
        algo_mode = (algo_mode + 1) % 3;
    }

    UpdateLedPattern(algo_mode);

    // Knob: 0-6 octaves. CV: -5 to +5V (1V/oct).
    float exponent = (k0 * 6.0f) + ((cv_pitch * 10.0f) - 5.0f);
    float base_freq = 50.0f * powf(2.0f, exponent);
    RecomputeIncrements(base_freq);

    // Map knobs according to algorithm mode
    // Common amplitude scaling (fixed or could use one knob if desired)
    float out_amp = 0.8f;

    // Prepare algorithm-specific parameters
    if(algo_mode == 0) {
        // Parallel: three pots are modulation indices for ops 1,2,3
        ops[1].mod_index = k1 * 8.0f;
        ops[2].mod_index = k2 * 8.0f;
        ops[3].mod_index = k3 * 8.0f;
        // Fixed ratios
        ops[1].ratio = 2.0f;
        ops[2].ratio = 3.0f;
        ops[3].ratio = 4.0f;
        RecomputeIncrements(base_freq);
    }
    else if(algo_mode == 1) {
        // Serial: pots select ratios (quantized) for ops 1..3, indices derived from positions subtly
        static const float harmonic[] = {0.5f,1.0f,1.5f,2.0f,3.0f,4.0f,5.0f,6.0f,8.0f};
        auto pick = [](float k){ int idx = (int)(k * 8.999f); if(idx < 0) idx = 0; if(idx > 8) idx = 8; return harmonic[idx]; };
        ops[1].ratio = pick(k1);
        ops[2].ratio = pick(k2);
        ops[3].ratio = pick(k3);
        ops[1].mod_index = 4.0f; // fixed moderate indices for clarity
        ops[2].mod_index = 3.0f;
        ops[3].mod_index = 2.0f;
        RecomputeIncrements(base_freq);
    }
    else { // Feedback mode (algo_mode == 2)
        // Feedback: k1 = feedback depth, k2,k3 = modulation indices for ops2 & ops3
        ops[1].ratio = 2.0f;
        ops[2].ratio = 2.5f;
        ops[3].ratio = 3.0f;
        ops[1].mod_index = 0.0f; // will be used via feedback
        ops[2].mod_index = k2 * 10.0f;
        ops[3].mod_index = k3 * 10.0f;
        RecomputeIncrements(base_freq);
        // Store feedback depth in ops[1].last_out temporarily scaled
        ops[1].last_out = k1 * 6.0f; // feedback amount
    }

    for(size_t i = 0; i < size; i++) {
        bool gate = hw.gate_in_1.State();
        if(gate)
            gate_seen = true;
        float env_amp = gate_seen ? env.Process(gate) : 1.0f;
        float mod = 0.0f;
        if(algo_mode == 0) {
            // Parallel stacking
            float m1 = FastSin(ops[1].phase); ops[1].phase = WrapPhase(ops[1].phase + ops[1].incr);
            float m2 = FastSin(ops[2].phase); ops[2].phase = WrapPhase(ops[2].phase + ops[2].incr);
            float m3 = FastSin(ops[3].phase); ops[3].phase = WrapPhase(ops[3].phase + ops[3].incr);
            mod = (m1 * ops[1].mod_index + m2 * ops[2].mod_index + m3 * ops[3].mod_index) * ops[0].incr;
        }
        else if(algo_mode == 1) {
            // Serial chain: op3 -> op2 -> op1 -> carrier
            float m3 = FastSin(ops[3].phase); ops[3].phase = WrapPhase(ops[3].phase + ops[3].incr);
            float m2_mod = m3 * ops[3].mod_index * ops[2].incr;
            float m2 = FastSin(ops[2].phase + m2_mod); ops[2].phase = WrapPhase(ops[2].phase + ops[2].incr + m2_mod);
            float m1_mod = m2 * ops[2].mod_index * ops[1].incr;
            float m1 = FastSin(ops[1].phase + m1_mod); ops[1].phase = WrapPhase(ops[1].phase + ops[1].incr + m1_mod);
            mod = (m1 * ops[1].mod_index) * ops[0].incr; // final serial modulation
        }
        else { // Feedback mode
            float fb_in = FastSin(ops[1].phase);
            float m1 = FastSin(ops[1].phase + fb_in * ops[1].last_out * ops[1].incr);
            ops[1].phase = WrapPhase(ops[1].phase + ops[1].incr + fb_in * ops[1].last_out * ops[1].incr);
            float m2 = FastSin(ops[2].phase); ops[2].phase = WrapPhase(ops[2].phase + ops[2].incr);
            float m3 = FastSin(ops[3].phase); ops[3].phase = WrapPhase(ops[3].phase + ops[3].incr);
            mod = (m1 * ops[1].last_out + m2 * ops[2].mod_index + m3 * ops[3].mod_index) * ops[0].incr;
        }

        // If B8 (edit mode) is ON, knobs edit envelope and volume
        if(shift_switch.Pressed()){
            float a = 0.001f + k1 * 0.5f;  // attack 1ms..500ms
            float r = 0.02f  + k2 * 1.2f;  // release 20ms..1.22s
            env.SetTime(ADSR_SEG_ATTACK, a);
            env.SetTime(ADSR_SEG_RELEASE, r);
            master_gain = 0.2f + k3 * 0.8f; // 0.2 .. 1.0
        }
        float sample = FastSin(ops[0].phase + mod) * env_amp * master_gain;
        ops[0].phase = WrapPhase(ops[0].phase + ops[0].incr + mod);
        out[0][i] = sample;
        out[1][i] = sample;
    }
}

int main(void) {
    hw.Init();
    hw.SetAudioBlockSize(48);
    InitSynth();
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    while(1)
    {
        UpdateLedPattern(algo_mode);
        System::Delay(5);
    }
}