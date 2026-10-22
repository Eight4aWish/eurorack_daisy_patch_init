#!/bin/bash
# The Wasp on Res 0-7 only, where it repeats itself (CLAUDE.md, "Res 0-7"), queued behind
# the Swords Res 0-4 V runs on the Mac mini (David, 2026-10-22):
#   1. the LP series' cutoff + Res GRUs on marks 0-7, Res 5 held out, otherwise lp_cr_*'s
#      recipe exactly (tools/train_lp_playable.sh): GRU-16 and GRU-24, seeds 1-3;
#   2. the control test over marks 0-7 on these and on lp_cr_*, written beside the first
#      scoring as control_response_m07.json;
#   3. HP: on marks 0-7 if LP 0-7 was promising, else the whole range. "Promising" was
#      fixed before any result: over marks 0-7, the new runs' median Res right way at least
#      0.10 above lp_cr_*'s, and their median amount no lower.
#
#     nohup caffeinate -sim tools/queue_wasp_res07.sh > captures/wasp/gru_runs/res07.log 2>&1 < /dev/null &

set -u
cd "$(dirname "$0")/.."
PY=.venv-nam/bin/python
RUNS=captures/wasp/gru_runs
M07=0,1,2,3,4,5,6,7
KEEP07=0,1,2,3,4,6,7

until grep -q "^finished:" captures/swords/sw4_a.log 2>/dev/null; do sleep 120; done
echo "Swords Res 0-4 V finished; Wasp LP 0-7 from $(date)"

run() {  # a run name, then train_gru.py's options
    local name=$1
    shift
    mkdir -p "$RUNS/$name"
    $PY tools/train_gru.py --name "$name" --threads 1 --clip 10 --listen "$@" > "$RUNS/$name/log.txt" 2>&1 &
}

$PY tools/train_gru.py --series lp --res $KEEP07 --holdout-res 5 --prepare > "$RUNS/lp07_split.txt" || exit 1
for h in 16 24; do
    for s in 1 2 3; do
        run "lp07_cr_gru${h}_s$s" --series lp --res $KEEP07 --holdout-res 5 --inputs audio,cv,res --hidden "$h" --seed "$s"
    done
done
wait
$PY tools/control_response.py --marks $M07 --json control_response_m07.json \
    "$RUNS"/lp_cr_gru* "$RUNS"/lp07_cr_gru* > "$RUNS/lp07_control.txt" 2>&1

verdict=$($PY - <<'EOF'
import glob, json, statistics
def med(pattern, key):
    v = [json.load(open(f))["res"][key] for f in glob.glob(f"captures/wasp/gru_runs/{pattern}/control_response_m07.json")]
    v = [x for x in v if x is not None]
    return statistics.median(v) if v else float("nan"), len(v)
(rw_old, n_old), (rw_new, n_new) = med("lp_cr_gru*", "right_way"), med("lp07_cr_gru*", "right_way")
am_old, am_new = med("lp_cr_gru*", "amount")[0], med("lp07_cr_gru*", "amount")[0]
ok = rw_new - rw_old >= 0.10 and am_new >= am_old
print(f"{'promising' if ok else 'not promising'}: over marks 0-7, right way {rw_old:.2f} -> {rw_new:.2f}, "
      f"amount {am_old:.2f} -> {am_new:.2f} ({n_old} and {n_new} runs)")
EOF
)
echo "$verdict" | tee "$RUNS/lp07_verdict.txt"

if [[ $verdict == promising* ]]; then
    HPKEEP=(--res $KEEP07); HPNAME=hp07_cr; HPMARKS=$M07
else
    HPKEEP=(); HPNAME=hp_cr; HPMARKS=0,2,4,5,6,8,10
fi
echo "HP: $HPNAME from $(date)"
$PY tools/train_gru.py --series hp "${HPKEEP[@]}" --holdout-res 5 --prepare > "$RUNS/${HPNAME}_split.txt" || exit 1
for h in 16 24; do
    for s in 1 2 3; do
        run "${HPNAME}_gru${h}_s$s" --series hp "${HPKEEP[@]}" --holdout-res 5 --inputs audio,cv,res --hidden "$h" --seed "$s"
    done
done
wait
$PY tools/control_response.py --marks $HPMARKS "$RUNS"/${HPNAME}_gru* > "$RUNS/${HPNAME}_control.txt" 2>&1
echo "finished: $(date)"
