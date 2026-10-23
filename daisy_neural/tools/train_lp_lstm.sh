#!/bin/bash
# The LSTM runs (2026-10-23, David: did we give up on LSTM too early?). The pilot of
# 2026-10-19 tried LSTMs on one take with no controls, one seed each, and could not tell them
# from the GRUs; every network with cutoff and Res inputs since has been a GRU. Only the cell
# changes against the first recipe's cutoff + Res runs (lp_cr_*): the same ESR loss,
# schedule, early stopping, clip at 10, data, splits (Res 5 held out) and seeds, and
# PyTorch's default initialisation as the GRUs had (no forget-gate bias). LSTM-14 (about
# GRU-16's weights: 1,064 against 1,008) and LSTM-16 (GRU-16's units), seeds 1-3, six side
# by side on the Mac mini, one thread each. Then the control-response test on these and the
# baseline in one run, so both are measured the same way.
#
#   nohup caffeinate -sim tools/train_lp_lstm.sh > captures/wasp/gru_runs/lp_lstm.log 2>&1 < /dev/null &

set -u
cd "$(dirname "$0")/.."
PY=.venv-nam/bin/python
RUNS=captures/wasp/gru_runs
COMMON="--series lp --holdout-res 5 --inputs audio,cv,res --cell lstm --clip 10 --listen --threads 1"

$PY tools/train_gru.py $COMMON --prepare > /dev/null || exit 1

for h in 14 16; do
    for s in 1 2 3; do
        name="lp_lstm${h}_s$s"
        mkdir -p "$RUNS/$name"
        $PY tools/train_gru.py --name "$name" $COMMON --hidden "$h" --seed "$s" > "$RUNS/$name/log.txt" 2>&1 &
    done
done
wait
$PY tools/control_response.py "$RUNS"/lp_lstm* "$RUNS"/lp_cr_gru* > "$RUNS/lp_lstm_control.txt" 2>&1
echo "finished: $(date)"
