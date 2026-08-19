// daisy_multiosc — boot-selectable multi-voice firmware for Daisy Patch Init.
//
// Hosts several small voice engines behind one universal panel (docs/PANEL.md).
// Pick an engine at power-on; B7 cycles (short) and toggles Play/Edit (long).
//
// Copyright (c) 2026 David Baghurst — MIT (see repo LICENSE).

#include "multiosc_core/host.h"
#include "fm4op_voice.h"
#include "intervalosc_voice.h"
#include "scanned_voice.h"
#include "bytebeat_voice.h"
#include "sine_engine.h"

using namespace multiosc;

static Host             host;
static FmFourOp         fm4op;
static IntervalOscVoice interval;
static ScannedVoice     scanned;
static BytebeatVoice    bytebeat;
static SineEngine       sine;

int main(void)
{
    host.AddEngine(&fm4op);
    host.AddEngine(&interval);
    host.AddEngine(&scanned);
    host.AddEngine(&bytebeat);
    host.AddEngine(&sine);
    host.Run(); // does not return
}
