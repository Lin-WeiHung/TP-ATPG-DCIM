#!/usr/bin/env bash
# sweep_weights.sh — 測試不同 scorer 權重組合對未達 100% 的 input case 的影響
set -euo pipefail
cd "$(dirname "$0")/.."

BINARY="./cim-atpg"
OUT_CSV="output/weight_sweep_results.csv"

# 22 個未達 100% 的 case
CASES=(
  v01_drop_CFid_up_0_.json
  v01_drop_CFid_up_1_.json
  v01_drop_CIDDB_1_1_.json
  v01_drop_CIDWDB.json
  v01_drop_CIDD_0_0_.json
  v01_drop_CIDD_1_0_.json
  v01_drop_IC00.json
  v01_drop_CI_10_11_.json
  v01_drop_CIDCB_0_00_.json
  v01_drop_CIDCB_1_11_.json
  v02_no_either_read_or_compute.json
  v03_no_single_cell.json
  v03_no_two_cell_cross_row.json
  v03_no_two_cell_same_row.json
  v04_no_family_CFid.json
  v04_no_family_CI.json
  v04_no_family_CIDCB.json
  v04_no_family_CIDD.json
  v04_no_family_CIDWD.json
  v04_no_family_DDCB.json
  v04_no_family_IC.json
  v05_two_cell_only.json
)

# 權重組合: (w_state, w_total, op_penalty)
# 原始 baseline: 0.9, 0.5, 0.01
WEIGHTS=(
  "0.9 0.5 0.01"
  "0.5 0.9 0.01"
  "0.0 1.0 0.01"
  "1.0 0.0 0.01"
  "0.7 0.7 0.01"
  "0.9 0.5 0.00"
  "0.5 0.9 0.00"
  "0.0 1.0 0.00"
  "1.0 0.0 0.00"
  "0.7 0.7 0.00"
  "0.9 0.5 0.05"
  "0.5 0.9 0.05"
  "0.3 0.9 0.01"
  "0.9 0.3 0.01"
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
    OUT_J="output/weight_sweep/${BASE}_ws${WS}_wt${WT}_op${OP}.json"
    OUT_H="output/weight_sweep/${BASE}_ws${WS}_wt${WT}_op${OP}.html"

    mkdir -p output/weight_sweep

    echo -n "[$IDX/$TOTAL_COMBOS] $CASE ws=$WS wt=$WT op=$OP ... "

    OUTPUT=$($BINARY --mode generate "$INPUT" "$OUT_J" "$OUT_H" \
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
echo "[WeightSweep] Results written to $OUT_CSV"
echo "[WeightSweep] Done!"
