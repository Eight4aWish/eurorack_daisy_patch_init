// daisy_multiosc — boot-selectable multi-voice firmware for Daisy Patch Init.
//
// Hosts several small voice engines behind one panel (docs/PANEL.md). It starts in the
// engine it was left in; hold B7 for 2 s without turning a knob for the next one, and
// hold B7 while turning a knob for the second bank (Volume, -, Attack, Decay).
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#include "multiosc_core/host.h"
#include "fm4op_voice.h"
#include "intervalosc_voice.h"
#include "scanned_voice.h"
#include "waves_engine.h"

using namespace multiosc;

static Host             host;
static FmFourOp         fm4op;
static IntervalOscVoice interval;
static ScannedVoice     scanned;
static WavesEngine      waves;

int main(void)
{
    host.SetVersion("v2.1"); // the 2026-11-22 interface (docs/PANEL.md); 2.1 fixed B7
    host.AddEngine(&fm4op);
    host.AddEngine(&interval);
    host.AddEngine(&scanned);
    host.AddEngine(&waves);
    host.Run(); // does not return
}
