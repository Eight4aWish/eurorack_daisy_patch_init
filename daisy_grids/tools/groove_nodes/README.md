# Deriving a drum-map bank

Grids is a lookup table, not an algorithm. `src/grids_nodes.cpp` is 25
hand-authored patterns of 96 bytes each — 3 instruments × 32 steps — and each
byte is a *level* from 0-255, not an on/off. X and Y pick a cell in a 5×5 grid
of those nodes and bilinearly interpolate between its four corners; density is
an inverted threshold on the result; accent is a fixed threshold at 192.

So the musicality lives entirely in about 2.4 KB of data. Replace it and you
have a different instrument with the same engine — which is what this does.

Mutable's own documentation says the original map came from "machine learning
techniques" over a large collection of drum loops, so the method was always
reproducible.

## The method

    python3 extract.py groove.zip patterns.npy   # a zip...
    python3 extract.py ~/midi/techno patterns.npy   # ...or any folder of MIDI
    python3 som.py                               # train the map, emit the table

`extract.py` takes a zip, a directory tree, or a single file. The directory case
is the useful one for a personal corpus: export patterns out of whatever
produced them, one folder per genre, and the parent directory becomes the label.
Nothing is redistributed, so deriving a bank from commercially licensed content
is fine — it just cannot be shipped, and does not need to be. The bank is 2.4 KB
of centroids over thousands of patterns; no source pattern survives in it.

**Corpus.** The [Groove MIDI Dataset](https://magenta.withgoogle.com/datasets/groove)
— 1,150 files, 13.6 hours of human drumming across rock, funk, afrobeat, hiphop,
jazz and latin, CC BY 4.0. `extract.py` reads MIDI directly (only note-on events
and the header's tick division are needed, so there is no dependency), maps GM
drum notes onto three lanes, quantises to 16ths and cuts 2-bar windows. 11,155
patterns survive the "at least 8 hits" filter.

**A self-organising map, not k-means.** Grids interpolates between *adjacent*
cells, so the arrangement has to preserve topology. k-means would give 25
sensible patterns in an arbitrary order and sweeping X/Y across them would jump
rather than morph. A 5×5 SOM clusters and arranges in one step. Measured, the
result is *more* topologically coherent than the original: neighbours differ 39%
less than random pairs, against 15% for Grids' own map.

**Per-lane histogram matching.** The SOM decides which steps matter and how they
rank; each lane then inherits its own value distribution from the original
tables. So active fraction, mean level and accent rate come out identical per
lane, and density thresholding and the 192 accent line behave exactly as they do
with Grids' data — the whole engine downstream is unchanged.

Matching *globally* instead was a mistake worth recording: the snare lane's
higher raw values captured most of the loud ranks and came out near-continuous
and all-strong, which is human ghost notes rendered as full hits. Per lane, the
ghosts land where they belong — low levels that only fire when density is up.

## What it sounds like

Human drummers, not electronic music. More ghost notes, more swing feel, less
machine regularity. The X axis runs from syncopated to straight four-on-the-
floor; the Y axis from dense to sparse.

## Other corpora worth the same treatment

- [WaivOps](https://www.patchbanks.com/waivops/) EDM-HSE, EDM-TR9, EDM-TECH —
  house, TR-909 and techno, CC BY 4.0. Tried and abandoned: the paired JSON
  carries MIDI note numbers and tempo but *no onset times*, so it says which
  drums are in a loop and never when they hit. Recovering the rhythm would mean
  onset-detecting 7.6 GB of audio per dataset.
- [MidiCaps](https://arxiv.org/pdf/2406.02255) — 168k MIDI files with genre
  captions, for filtering to a specific style.
- Grime, psytrance and liquid DnB have no clean open corpus. Their signatures
  are strong and few, so rule-generated archetypes fed through the same SOM
  would likely beat clustering a noisy corpus.

## Open: the SOM is the wrong tool, and the shipped banks are flat

**Reported by ear first** - "our banks do not move musically, it does not sound
like much is happening" - and the measurements agree. The number that matters is
how many steps change state when you move X or Y by one cell, at mid density:

| bank | per-edge | across the map | edge/map | dead edges |
| --- | ---: | ---: | ---: | ---: |
| Latin, as shipped | 12.1 | 26.3 | 46% | 3/40 |
| Traditional, as shipped | 11.7 | 24.4 | 48% | 2/40 |
| Club, as shipped | 12.0 | 20.8 | 58% | 6/40 |
| Grids' own | 13.8 | 17.6 | 78% | 0/40 |

*dead edge* = neighbours differing by three steps or fewer, i.e. a knob move you
cannot hear. Club has six of them.

**Why.** A self-organising map's objective function *is* to minimise the
difference between neighbouring cells - that is what topology preservation means.
So the tool was optimising for the property that makes the knob boring. Worse,
its nodes are cluster centroids: averages of thousands of patterns, which regress
toward each other. Twenty-five averages are less distinctive than twenty-five
archetypes, and the coherence score I was reporting as a quality mark was in
large part measuring that flatness approvingly.

**The fix, tested.** Pick twenty-five *real* patterns from the corpus by
farthest-point sampling, then arrange them on the 5x5 to minimise total edge
length - which is what Emilie did by hand:

| method | per-edge | across | edge/map | dead |
| --- | ---: | ---: | ---: | ---: |
| SOM centroids (shipped) | 12.1 | 26.3 | 46% | 3/40 |
| real patterns, unarranged | 37.9 | 35.5 | 106% | 0/40 |
| **real patterns, arranged** | **29.2** | 37.7 | **78%** | **0/40** |

Unarranged is 106%: neighbours differ more than distant cells, so every move is a
jump with no sense of travel. Arranging pulls it to 78% - which is Emilie's
number, arrived at independently. That is worth noticing: 78% looks like what
"distinct patterns, well arranged" settles at, and she got there by ear in 2011.

The result covers more than twice her range while moving 2.4x as much per knob
step as the bank we shipped.

**Not done.** Re-deriving means new firmware, new WAVs, new site copy, and the
call for testers is already live. Decision pending.
