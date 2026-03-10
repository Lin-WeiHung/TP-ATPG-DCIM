#!/usr/bin/env bash
# sweep_weights_quick.sh — 精簡版：只測最難的 case + 關鍵權重組合
set -euo pipefail
cd "$(dirname "$0")/.."

BINARY="./cim-atpg"
OUT_CSV="output/weight_sweep_quick.csv"

# 選 4 個最難的 case（覆蓋率最低）
CASES=(
  v01_drop_CIDCB_1_11_.json
  v04_no_family_CIDD.json
  v03_no_single_cell.json
  v05_two_cell_only.json
)

# 只測 4 組關鍵權重（根據已有數據的發現）
WEIGHTS=(
  "0.9 0.5 0.01"
  "0.9 0.5 0.00"
  "0.7 0.7 0.00"
  "0.0 1.0 0.00"
)

echo "case,w_state,w_total,op_penalty,best_coverage,best_config,reached_100,elapsed_ms" > "$OUT_CSV"

TOTAL_COMBOS=$(( ${#CASES[@]} * ${#WEIGHTS[@]} ))
IDX=0

for CASE in "${CASES[@]}"; do
  for W in "${WEIGHTS[@]}"; do
    read -r WS WT OP <<< "$W"
    IDX=$((IDX + 1))
    INPUT="input_list/$CASE"
    BASE=$(basename "$CASE" .json)

    echo -n "[$IDX/$TOTAL_COMBOS] $CASE ws=$WS wt=$WT op=$OP ... "

    OUTPUT=$($BINARY --mode generate "$INPUT" /dev/null /dev/null \
      --max-slots 4 --max-L 6 \
      --w-state "$WS" --w-total "$WT" --op-penalty "$OP" 2>&1) || true

    BEST_COV=$(echo "$OUTPUT" | grep -oP 'Best coverage: \K[0-9.]+' || echo "0")
    BEST_CFG=$(echo "$OUTPUT" | grep -oP 'Best config: \K\S+' || echo "N/A")
    REACHED=$(echo "$OUTPUT" | grep -oP 'Reached 100%: \K\S+' || echo "No")
    ELAPSED=$(echo "$OUTPUT" | grep -oP 'Total elapsed: \K[0-9]+' || echo "0")

    echo "cov=${BEST_COV}% (${ELAPSED}ms)"
    echo "$CASE,$WS,$WT,$OP,$BEST_COV,$BEST_CFG,$REACHED,$ELAPSED" >> "$OUT_CSV"
  done
done

echo ""
echo "[QuickSweep] Results written to $OUT_CSV"
