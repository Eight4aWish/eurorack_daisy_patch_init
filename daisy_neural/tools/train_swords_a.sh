#!/bin/bash
# Swords Part A (SWORDS.md): the cutoff + Res GRU on session sw-a, the Wasp's first recipe
# unchanged (ESR, the plateau schedule and early stopping, clip at 10), so only the data
# differs. Two arms from the same session:
#   matched  --halves 1    30 minutes of sweeps, about the Wasp's amount: filter against filter
#   all      --halves 1,2  60 minutes: whether more material helps Res
# GRU-16 and GRU-24, seeds 1-3: twelve runs, six at a time on the Mac mini, one thread each.
# Then the control test on all twelve, in the region step 1 judged, with the verdict.
#
# The one argument is the held-out Res band, volts, chosen from region.json:
#   nohup caffeinate -sim tools/train_swords_a.sh 1.8,2.2 > captures/swords/sw_a.log 2>&1 < /dev/null &

set -u
cd "$(dirname "$0")/.."
BAND=${1:?usage: train_swords_a.sh LO,HI (the held-out Res band, volts)}
PY=.venv-nam/bin/python
RUNS=captures/swords/gru_runs
COMMON="--swords --holdout-band $BAND --inputs audio,cv,res --clip 10 --listen --threads 1"
mkdir -p "$RUNS"

# Build the memory-mapped takes once, before six runs try to at the same time.
$PY tools/train_gru.py $COMMON --halves 1,2 --prepare > "$RUNS/sw_a_split.txt" || exit 1

for arm in matched all; do
    halves=$([ "$arm" = matched ] && echo 1 || echo 1,2)
    for h in 16 24; do
        for s in 1 2 3; do
            name="sw_${arm}_gru${h}_s$s"
            mkdir -p "$RUNS/$name"
            $PY tools/train_gru.py --name "$name" $COMMON --halves "$halves" --hidden "$h" --seed "$s" \
                > "$RUNS/$name/log.txt" 2>&1 &
        done
    done
    wait
done
$PY tools/control_response.py "$RUNS"/sw_* > "$RUNS/sw_a_control.txt" 2>&1
echo "finished: $(date)"
