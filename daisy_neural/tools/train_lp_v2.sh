#!/bin/bash
# The second LP recipe (2026-10-19). After NAM's trainer was reviewed, and the first GRUs'
# Res proved inaudible on the module: MSE on a target normalised to -18 dBFS plus NAM's
# multi-resolution STFT term (0.0005), one optimiser step per batch, Adam at 0.004 decaying
# x0.999 an epoch over a fixed 300 epochs, ESR picks the epoch, gradient clipped at 10 with
# non-finite steps skipped. Splits, data and seeds as the first recipe, whose runs are the
# baseline (lp_cr_*).
#   gru      GRU-16 and GRU-24, update gate started towards keeping its history (+1),
#            seeds 1-3, six side by side on the CPU (the Mac mini)
#   wavenet  NAM's WaveNet in A2-Lite's layout, audio, cutoff and Res as its conditioning,
#            FiLM before each activation; seeds 1-3, one after another on the GPU; 59
#            batches of 8,192 samples an epoch, the GRU's samples per epoch
# Then the control-response test on every run (and on the first recipe's, for comparison).
#
#   nohup caffeinate -sim tools/train_lp_v2.sh gru > captures/wasp/gru_runs/lp2_gru.log 2>&1 < /dev/null &
#   nohup caffeinate -sim tools/train_lp_v2.sh wavenet ~/wasp_lp > ~/wasp_lp/lp2_wavenet.log 2>&1 < /dev/null &

set -u
cd "$(dirname "$0")/.."
PY=.venv-nam/bin/python
MODE=$1
CAPTURES=${2:-captures/wasp}
RUNS=$CAPTURES/gru_runs
COMMON="--captures $CAPTURES --series lp --holdout-res 5 --inputs audio,cv,res --loss mse --mrstft-weight 0.0005
        --target-dbfs -18 --schedule exp --gamma 0.999 --lr 0.004 --clip 10 --patience 0 --epochs 300 --batch 32 --listen"

$PY tools/train_gru.py $COMMON --prepare > /dev/null || exit 1

run() {  # a run name, then the arm's own options
    local name=$1
    shift
    mkdir -p "$RUNS/$name"
    $PY tools/train_gru.py --name "$name" $COMMON "$@" > "$RUNS/$name/log.txt" 2>&1
}

if [ "$MODE" = gru ]; then
    for h in 16 24; do
        for s in 1 2 3; do
            run "lp2_gru${h}_s$s" --arch rnn --cell gru --hidden "$h" --step batch --z-bias 1 --seed "$s" --threads 1 &
        done
    done
    wait
    $PY tools/control_response.py --captures "$CAPTURES" "$RUNS"/lp2_gru* "$RUNS"/lp_cr_gru* > "$RUNS/lp2_gru_control.txt" 2>&1
elif [ "$MODE" = wavenet ]; then
    for s in 1 2 3; do
        run "lp2_wn_s$s" --arch wavenet --film activation_pre --seg 0.17067 --steps-per-epoch 59 \
            --device mps --threads 2 --seed "$s"
    done
    $PY tools/control_response.py --captures "$CAPTURES" "$RUNS"/lp2_wn* > "$RUNS/lp2_wn_control.txt" 2>&1
else
    echo "usage: $0 gru|wavenet [captures folder]" >&2
    exit 2
fi
echo "finished: $(date)"
