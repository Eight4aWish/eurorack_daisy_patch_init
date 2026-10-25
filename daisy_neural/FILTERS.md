# Mirth's filters — credits

The seven files in the FILTERS bank, learned from two filters in the eight4awish rack: a
**Doepfer A-124 Wasp** and a **Behringer Swords**. Each was recorded on the rack's own
module through an ES-10 at 48 kHz, its cutoff (and on Swords its resonance) swept by CV,
then trained on a Mac (`tools/train_gru.py`; the Wasp's sessions in `WASP.md`, Swords' in
`SWORDS.md`). Recorded and trained by David Baghurst, 2026-10.

| On the card | What it is |
|---|---|
| `f1_WASP_A2.a2nb` | the Wasp's low-pass at one setting (Res 6, cutoff CV +5 V), as an A2-Lite capture: it plays in FILTERS, through the amps' engine |
| `f2_WASP_R6_16.grnb` | a GRU-16 of the Wasp's low-pass at Res 6, cutoff under CV |
| `f3_WASP_LP_16.grnb` | a GRU-16 of the Wasp's low-pass, cutoff and Res |
| `f4_WASP_LP_GR.grnb` | f3 plus a granular layer: the Wasp's take-to-take randomness above Res 8, measured from its repeat takes |
| `f5_WASP_HP_16.grnb` | a GRU-16 of the Wasp's high-pass, cutoff and Res |
| `f6_SWORDS_16.grnb` | a GRU-16 of Swords, cutoff and Res (Res CV 0–4 V) |
| `f7_SWORDS_OSC.grnb` | f6 plus an oscillator layer: Swords' self-oscillation, built from measurements of it |

**Changes from the real filters:** each file carries its own knob ranges and a Res dial
table calibrated against the module it learned (`tools/grnb_controls.py`). Above Res 7 the
Wasp's networks are lifted, and from 9 fed back, because they learned the resonance more
weakly than the Wasp has it; 10 to 11 is over-resonance that the real filter never
reaches.

**Licence:** [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Use them, bend
them and share them, with credit to David Baghurst / Eight4aWish (eight4awish.com) and a
note of any changes.

Not affiliated with, or endorsed by, Doepfer or Behringer. Product names describe what
was recorded.
