# Capturing your own module

This is the path from a module in your rack to a filter on Mirth's card, in order. It is how
the Wasp and Swords filters were made (FLT2–FLT7). The full records, with every number, are
the two bench sheets: [WASP.md](WASP.md) and [SWORDS.md](SWORDS.md). Everything runs from
`daisy_neural/`, and the recordings land in `captures/`, which git ignores.

**What comes out** is a `.grnb` file for the FILTERS bank: a GRU, a small recurrent network
that reads the audio and up to two of the module's controls one sample at a time and carries
sixteen numbers of history forward (GRU-16: 1,025 parameters with three inputs). On the module
its two controls are the FREQ knob, which feeds the network's first control in volts, and the
RES dial, which feeds the second. DRIVE and MIX work as on every Mirth bank. Any other control
on your module is set once, noted with each session, and stays where it was.

**The scripts are written for those two modules.** `tools/wasp_capture.py` and
`tools/swords_capture.py` record the Wasp and Swords; a new module means copying the nearer one
and changing its session plan (see "Adapting the scripts", at the end).

## What you need

| | |
|---|---|
| **An interface** | DC-coupled, so it can send CV: three outputs and three inputs, four outputs if both controls are under CV. One cable from an output straight back to an input, the loopback, which times every take. The recordings here went through an Expert Sleepers ES-10 on a Focusrite Scarlett 16i16's ADAT port, with the ES-10's DC jumper set (WASP.md, "Once, before the first session"); an ES-8 or ES-9 should do, but the scripts have only run on the ES-10. |
| **A multimeter** | DC volts, once, for the rig check. |
| **Python 3.12, twice** | `.venv` for recording and splitting; `.venv-nam` for training and export, NAM's trainer, which brings PyTorch (below). |
| **Disk** | The Wasp's folder came to 36 GB and Swords' to 27 GB: recordings, split files and training runs together. `rec/` in each session is the one thing that cannot be rebuilt. |
| **Time** | Half an hour of warm-up before recording. The Wasp took about 75 minutes of takes per series (low-pass and high-pass), one session per Res mark; Swords, with both controls under CV, one 85-minute session. One GRU-16 run (`lp_cr_gru16_s3`, FLT3) trained for 144 minutes on a Mac mini, and each network on the card was chosen from three seeds trained side by side. |

```
/opt/homebrew/bin/python3.12 -m venv .venv && .venv/bin/pip install -r requirements.txt
/opt/homebrew/bin/python3.12 -m venv .venv-nam
.venv-nam/bin/pip install "neural-amp-modeler==0.13.0"
brew install python-tk@3.12      # NAM imports Tk even to train without its GUI
```

## 1. Is it worth capturing?

A network can only learn what the module does every time. Two things decide it.

- **Does it repeat itself?** Play the same input several times at a few settings and compare
  the takes. The Wasp repeated itself to ESR 0.0003 at Res 6 and 0.0015 at Res 7, then 0.073,
  0.39 and 0.87 at 8, 9 and 10 (WASP.md, section 7). Above Res 8 the takes differ more than
  any network can follow, so the networks there sound tamer than the Wasp; FLT4 puts the
  wildness back with a layer of grains taken from repeat takes (`gru_export.py --layer`).
  The Wasp's way: repeat sessions of the `test` take, compared with `drift`. Swords' way: three
  back-to-back takes at several Res CVs in the session itself, read with `region` against a rule
  fixed before recording (take-to-take ESR 0.02 or less per hold).
- **Does it self-oscillate?** Where an oscillation starts in its cycle changes from take to
  take, and no network can learn that. Swords repeated itself to the rig's floor below Res CV
  4 V; past its onset, everything it played repeated except that phase. So its network was
  trained on 0–4 V, and FLT7 adds an oscillator built from measurements (SWORDS.md, "Above
  the threshold" and "SWORDS_OSC").

## 2. Choose the controls

- **Under CV: swept inside each take.** The interface moves it continuously while the audio
  plays: the Wasp's cutoff on CV 1; Swords' cutoff on V/OCT and its Res on the Res CV input,
  attenuverter fully clockwise.
- **Knob only: stepped between sessions.** One session per position, the knob set by hand:
  the Wasp's Res, at all eleven marks of its 0–10 dial. Setting a knob by eye costs some
  accuracy (about 0.01 ESR at Res 6–7 on the Wasp), so a CV input is better where there is one.
- **Everything else fixed**, at an end stop where possible, since an end stop repeats exactly
  and a mark on a dial doesn't. Photograph the panel and note it in each session (`--knobs`,
  `--panel`).
- **Hold one setting back** and never train or validate on it: Res 5 on the Wasp, the Res CV
  band 1.8–2.2 V on Swords. It tests whether the network fills a gap in the dial.
- **Find the ranges first.** The Wasp: a listening tour, the test riff once per volt
  (`make ladder`), to choose the cutoff range and the fixed knobs. Swords: `make cal`, which
  measures the cutoff against V/OCT and where self-oscillation starts and stops.

## 3. Check the rig

The meter first: unplug the loopback lead from its input, put the multimeter across it, play
the meter file and read it. Plug it back in, then the check, with the meter's reading:

```
.venv/bin/python tools/wasp_capture.py devices
.venv/bin/python tools/wasp_capture.py make meter
.venv/bin/python tools/wasp_capture.py record captures/wasp/meter.wav
.venv/bin/python tools/wasp_capture.py make rigcheck
.venv/bin/python tools/wasp_capture.py record captures/wasp/rigcheck.wav
.venv/bin/python tools/wasp_capture.py check captures/wasp/rigcheck.rec.wav --meter-volts 5.48 --save
```

`--save` writes `rig.json`: the device, the channels, the volts per full scale out and in, so
every later file is made in volts. The file appears with the first `make rigcheck`: fill in
`device` and the channel numbers before recording it. What good looks like, on 2026-10-18: a latency of
12,369 samples, "DC gets through", a loopback gain of 0.916, and the module 66 dB over its
floor once the filter opens (WASP.md, "1. Rig check").

## 4. Record

Each session is made, then recorded, then split. `record` plays every take in turn, skips any
already recorded, and refuses to start a take it can't save. Don't touch the rack while it
runs.

```
# the Wasp: one session per Res mark
.venv/bin/python tools/wasp_capture.py make session lp-res6 --mix lp --res 6 --cv-range=0,10 \
    --snapshots=2,3.5,5,6.5,8 --sweep-minutes 4 --knobs "Lev 10, Frq 0"
.venv/bin/python tools/wasp_capture.py record captures/wasp/lp-res6
.venv/bin/python tools/wasp_capture.py split captures/wasp/lp-res6

# Swords: one session, both controls under CV
.venv/bin/python tools/swords_capture.py make session sw-a --cv-range=2.5,10.5 --res-range=0,6 \
    --test-res=0,1,2,3,3.5,4,4.5,5.5
.venv/bin/python tools/swords_capture.py record captures/swords/sw-a
.venv/bin/python tools/swords_capture.py split captures/swords/sw-a
.venv/bin/python tools/swords_capture.py region captures/swords/sw-a
```

- **Interleave the sessions** so slow drift doesn't line up with a control: the Wasp's order
  was Res 6, the evens, 6 again, the odds, 6 a third time. The three Res 6 test takes measure
  the drift (`drift`).
- **The `test` take is identical in every session**: a riff no network trains on, with the
  cutoff held at six voltages and ramped. Every comparison is made on it.
- **Each take should split as OK.** `DROPOUT` means samples were lost: record it again.
  `full scale reached` means an output clipped the interface: turn the module's level down
  and keep it there. The full list is in WASP.md, "4. Split and check".

## 5. Train

`tools/train_gru.py`, in `.venv-nam`. It splits the sessions itself: training on the sweeps;
validation on three blocks in every sweep take, which choose the epoch; and a test, scored
once, of every `test` take and the setting held back. Build the shared copies and see the split
first, then launch the seeds:

```
.venv-nam/bin/python tools/train_gru.py --series lp --holdout-res 5 --prepare
.venv-nam/bin/python tools/train_gru.py --name lp_cr_gru16_s1 --series lp --holdout-res 5 \
    --inputs audio,cv,res --hidden 16 --seed 1 --threads 1 --clip 10 --listen
```

| Flag | |
|---|---|
| `--inputs` | `audio`, `audio,cv` (one control) or `audio,cv,res` (two) |
| `--hidden` | the history's size: 16 on the card; the card plays up to GRU-24 |
| `--seed` | 1, 2, 3: train several and keep the best on validation |
| `--holdout-res`, `--holdout-band` | the setting held back: a Res mark (Wasp), or a band of Res CV volts (Swords, with `--swords`) |
| `--res-max` | Swords: train and validate only below these volts (4 for FLT6) |
| `--threads 1` | one CPU thread per run, so several runs share a machine |
| `--listen` | write the chosen epoch's output on every test take |
| `--device mps` | the GPU on an Apple Silicon Mac; `cpu` is the default |

The two series' launch scripts, `tools/train_lp_playable.sh` and `tools/train_swords_a.sh`,
start the seeds side by side under `caffeinate`. `tools/gru_status.py` serves a status page
on port 8124 to watch them from another computer (WASP.md, "6. Train the GRUs").

## 6. Judge it

Each run writes `result.json`: validation and test ESR, per setting, and on the one held
back. ESR, error-to-signal ratio, is the share of the module's output the network fails to
reproduce: 0 is a perfect copy, 1 as wrong as silence. NAM's grades are under 0.01 great,
under 0.035 not bad and under 0.1 might sound OK. The Wasp's cutoff + Res GRU-16s scored 0.031
at Res 5, the mark they never heard, averaged over three seeds.

Then listen. `--listen` leaves the network's output on every test take beside the real
module's; subtract one from the other and what's left is what the network didn't learn.
Before anything goes on the module, `tools/gru_check.py` runs the exported file through the
firmware's own engine on the desktop and compares it with PyTorch (build `tools/gru_host`
first; the commands are at the top of `tools/gru_host.cpp`).

## 7. Export, set the controls, and copy to the card

```
.venv-nam/bin/python tools/gru_export.py captures/wasp/gru_runs/lp_cr_gru16_s3 --name "WASP LP 16"
.venv/bin/python tools/grnb_controls.py f3_WASP_LP_16.grnb --cut 0,10 \
    --dial 0:0:0,7:7:0,9:12:0,10:12:0.15,11:12:0.5
```

- **`gru_export.py`** writes the run's best epoch as a `.grnb`. It takes a GRU of one layer,
  with a hidden size that is a multiple of 4.
- **`grnb_controls.py`** writes the file's controls: the network's cutoff volts at the FREQ
  knob's ends (`--cut`), and the Res dial's table (`--dial`, dial:Res:feedback points), which
  is what lets the dial go to 11. The tables on the card were measured against their own
  modules with `tools/filter_dial_calibrate.py` (the Wasp HP and Swords) and
  `tools/res_dial_calibrate.py` (the Wasp LP); a new module needs its own entry.
- **The level match**, so DRIVE at noon puts volts in and gets volts out as your module did:
  `--audio-scale` is 7.30 divided by your interface's volts per full scale out, and `--gain`
  its volts per full scale in divided by 8.85 (the patch.init's own full scales, measured by
  `tools/mirth_input_fs.py`). Both are in your `rig.json`. On the ES-10: 0.666 and 1.353.
- **The card.** Filters play in filename order, up to sixteen, so name yours to come after the
  seven (`f8_…`). The card is one FAT32 partition of 2 GB or less; after copying, delete the
  `._` files macOS writes beside each one (README, "Captures on the microSD card").

## Adapting the scripts

What is general, and works for any module: `rig.json` (the device, its channels and volts),
the signals and timing marks, the loopback alignment, the drop-out check and the split.
What belongs to one module, and changes with it:

- **The session plan** in `make session`: which control is swept on which output, which is
  stepped between sessions, the ranges, and the repeat takes. Copy `wasp_capture.py` if one
  control is under CV and the other is a knob, `swords_capture.py` if both are under CV.
- **The patch**, in each script's opening comment, and the names of the outputs recorded
  (`y_lphp` and `y_bp` for the Wasp, `y_f1` for Swords).
- **The trainer's session reader**: `--series` reads the Wasp's sessions, `--swords` Swords';
  a new module needs one alongside them in `train_gru.py`.

If you would like to capture a module with me, email info@eight4awish.com: I will help train
and compare the networks.
