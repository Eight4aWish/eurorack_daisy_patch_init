#!/bin/bash
# The LP series' playable GRUs, as designed on 2026-10-19 (CLAUDE.md, the Wasp section):
#   cutoff and Res, every mark but Res 5: GRU-16 and GRU-24, three seeds each;
#   cutoff only, at Res 6, sweeps plus the NAM-file takes: GRU-16 and GRU-24, two seeds each.
# Ten runs side by side, one thread each. From daisy_neural/, with captures/wasp attached;
# caffeinate keeps the machine awake until the last run finishes. It needs -s: started
# over SSH, the Mac is only in a network "dark wake", and -i alone lets it fall back
# into maintenance sleep with the runs paused (seen on the Mac mini, 2026-10-19):
#
#     nohup caffeinate -sim tools/train_lp_playable.sh > captures/wasp/gru_runs/lp_playable.log 2>&1 < /dev/null &
#
# Each run logs to captures/wasp/gru_runs/<name>/log.txt.

set -u
cd "$(dirname "$0")/.."
PY=.venv-nam/bin/python
RUNS=captures/wasp/gru_runs

# Build the memory-mapped takes once, before ten runs try to at the same time.
$PY tools/train_gru.py --series lp --holdout-res 5 --prepare > /dev/null || exit 1
$PY tools/train_gru.py --series lp --res 6 --snapshots --prepare > /dev/null || exit 1

run() {  # a run name, then train_gru.py's options
    local name=$1
    shift
    mkdir -p "$RUNS/$name"
    $PY tools/train_gru.py --name "$name" --threads 1 --listen "$@" > "$RUNS/$name/log.txt" 2>&1 &
}

for h in 16 24; do
    for s in 1 2 3; do
        run "lp_cr_gru${h}_s$s" --series lp --holdout-res 5 --inputs audio,cv,res --hidden "$h" --seed "$s"
    done
    for s in 1 2; do
        run "lp6_c_gru${h}_s$s" --series lp --res 6 --snapshots --inputs audio,cv --hidden "$h" --seed "$s"
    done
done
wait
echo "all ten runs finished: $(date)"
