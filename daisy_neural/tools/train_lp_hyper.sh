#!/bin/bash
# The hypernetwork runs (2026-10-20, David's option (a)): a GRU whose every weight is a
# smooth function of the Res dial (w0 + P1(r) w1 + P2(r) w2), Res no longer an input,
# the cutoff still one. Only the architecture changes against the first recipe's
# cutoff + Res runs (lp_cr_*): the same ESR loss, schedule, early stopping, clip at 10,
# data, splits (Res 5 held out) and seeds. GRU-16 and GRU-24, seeds 1-3, six side by side
# on the Mac mini, one thread each. Then the control-response test on these and the
# baseline.
#
#   nohup caffeinate -sim tools/train_lp_hyper.sh > captures/wasp/gru_runs/lp_hyper.log 2>&1 < /dev/null &

set -u
cd "$(dirname "$0")/.."
PY=.venv-nam/bin/python
RUNS=captures/wasp/gru_runs
COMMON="--series lp --holdout-res 5 --inputs audio,cv --arch hyper --hyper-degree 2 --clip 10 --listen --threads 1"

$PY tools/train_gru.py $COMMON --prepare > /dev/null || exit 1

for h in 16 24; do
    for s in 1 2 3; do
        name="lp_hyp_gru${h}_s$s"
        mkdir -p "$RUNS/$name"
        $PY tools/train_gru.py --name "$name" $COMMON --hidden "$h" --seed "$s" > "$RUNS/$name/log.txt" 2>&1 &
    done
done
wait
$PY tools/control_response.py "$RUNS"/lp_hyp_gru* "$RUNS"/lp_cr_gru* > "$RUNS/lp_hyper_control.txt" 2>&1
echo "finished: $(date)"
