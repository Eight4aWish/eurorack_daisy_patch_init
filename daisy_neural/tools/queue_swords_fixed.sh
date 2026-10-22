#!/bin/bash
# Swords at fixed Res (SWORDS.md, "Fixed Res"): the eight sw-b sessions, two families of
# GRU-16 (audio, cutoff, Res), seed 1, Part A's recipe otherwise:
#   independent  swf_ind_r<Res>   each from random weights, on its own session
#   shared       swf_parent       one network on all eight sessions, then
#                swf_ft_r<Res>    a copy of it trained on each session at a fifth of the rate
# Res 2 V is in no session: it is the held-out test of blending the 1.5 and 2.5 V networks.
# At most four runs at a time, in the cores the Res 0-4 V and Wasp queues leave free.
#
#     nohup caffeinate -sim tools/queue_swords_fixed.sh > captures/swords/swf.log 2>&1 < /dev/null &

set -u
cd "$(dirname "$0")/.."
PY=.venv-nam/bin/python
RUNS=captures/swords/gru_runs
RES="0.00 0.50 1.00 1.50 2.50 3.00 3.50 4.00"
COMMON="--swords --inputs audio,cv,res --hidden 16 --seed 1 --clip 10 --listen --threads 1"
mkdir -p "$RUNS"

slot() {  # wait for one of this queue's four places
    while [ "$(jobs -rp | wc -l)" -ge 4 ]; do sleep 30; done
}
run() {  # a run name, then train_gru.py's options
    local name=$1
    shift
    mkdir -p "$RUNS/$name"
    $PY tools/train_gru.py --name "$name" $COMMON "$@" > "$RUNS/$name/log.txt" 2>&1 &
}

$PY tools/train_gru.py $COMMON --sessions 'sw-b-r*' --prepare > "$RUNS/swf_split.txt" || exit 1
run swf_parent_gru16_s1 --sessions 'sw-b-r*'
PARENT=$!
for r in $RES; do
    slot
    run "swf_ind_r${r}_gru16_s1" --sessions "sw-b-r$r"
done
wait "$PARENT"
[ -f "$RUNS/swf_parent_gru16_s1/best.pt" ] || { echo "the parent did not finish: $(date)"; exit 1; }
echo "parent finished: $(date)"
for r in $RES; do
    slot
    run "swf_ft_r${r}_gru16_s1" --sessions "sw-b-r$r" --init-from "$RUNS/swf_parent_gru16_s1" --lr 1e-3
done
wait
echo "finished: $(date)"
