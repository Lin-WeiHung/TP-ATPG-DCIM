#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

BINARY="./cim-atpg"
INPUT_DIR="input_list"
OUTPUT_DIR="output_20260311_twophase"
SUMMARY_CSV="$OUTPUT_DIR/summary.csv"

mkdir -p "$OUTPUT_DIR"

echo "file,before_cov,before_ops,after_cov,after_ops,compress_ops" > "$SUMMARY_CSV"

FILES=($(ls "$INPUT_DIR"/*.json | grep -v manifest | sort))
TOTAL=${#FILES[@]}

echo "========================================"
echo " Two-phase batch: coverage-first + compress"
echo " Total files: $TOTAL"
echo "========================================"
echo ""

IDX=0
for INPUT_FILE in "${FILES[@]}"; do
    IDX=$((IDX + 1))
    FNAME=$(basename "$INPUT_FILE")
    BASE=$(basename "$FNAME" .json)
    OUT_JSON="$OUTPUT_DIR/${BASE}.json"
    OUT_HTML="$OUTPUT_DIR/${BASE}.html"

    echo -n "[$IDX/$TOTAL] $FNAME ... "

    OUTPUT=$($BINARY --mode generate "$INPUT_FILE" "$OUT_JSON" "$OUT_HTML" \
        --max-slots 4 --max-L 6 --op-penalty 0.015 2>&1) || true

    # Sweep best (before refine)
    REFINE_BEFORE=$(echo "$OUTPUT" | grep -oP '\[Refine\] Before: \K[0-9.]+' || echo "")
    REFINE_AFTER=$(echo "$OUTPUT" | grep -oP '\[Refine\] After:\s+\K[0-9.]+' || echo "")

    if [ -z "$REFINE_BEFORE" ]; then
        BEST_COV=$(echo "$OUTPUT" | grep -oP '\[Sweep\] Best coverage: \K[0-9.]+' || echo "0")
        REFINE_BEFORE="$BEST_COV"
        REFINE_AFTER="$BEST_COV"
    fi

    # Before-refine ops
    BEST_SLOTS=$(echo "$OUTPUT" | grep -oP '\[Refine\] Greedy best slots=\K[0-9]+' || echo "")
    if [ -n "$BEST_SLOTS" ]; then
        BEFORE_OPS=$(echo "$OUTPUT" | grep -P "\[Sweep\].*slots=$BEST_SLOTS," | grep -oP '\(ops=\K[0-9]+' | tail -1 || echo "N/A")
    else
        BEFORE_OPS=$(echo "$OUTPUT" | grep -oP '\[Compress\] Before: \K[0-9]+' || echo "N/A")
    fi

    # After refine ops (from Compress Before line, since that's what refine produced)
    AFTER_REFINE_OPS=$(echo "$OUTPUT" | grep -oP '\[Compress\] Before: \K[0-9]+' || echo "N/A")

    # Final compressed ops (from Report line)
    FINAL_OPS=$(echo "$OUTPUT" | grep -oP '\[Report\].*\(ops=\K[0-9]+' || echo "N/A")
    FINAL_COV=$(echo "$OUTPUT" | grep -oP '\[Sweep\] Best coverage: \K[0-9.]+' || echo "0")

    echo "sweep=${REFINE_BEFORE}%/${BEFORE_OPS}ops -> refine=${REFINE_AFTER}%/${AFTER_REFINE_OPS}ops -> final=${FINAL_COV}%/${FINAL_OPS}ops"
    echo "$FNAME,$REFINE_BEFORE,$BEFORE_OPS,$REFINE_AFTER,$AFTER_REFINE_OPS,$FINAL_OPS" >> "$SUMMARY_CSV"
done

echo ""
echo "========================================"
echo " Done! Summary: $SUMMARY_CSV"
echo "========================================"
