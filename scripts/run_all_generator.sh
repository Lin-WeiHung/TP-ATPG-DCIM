#!/usr/bin/env bash
# run_all_generator.sh - Run generator mode on every JSON in input_list/ and collect summary
set -euo pipefail
cd "$(dirname "$0")/.."

BINARY="./cim-atpg"
INPUT_DIR="input_list"
OUTPUT_DIR="output_20260310_1607"
SUMMARY_CSV="$OUTPUT_DIR/summary.csv"

mkdir -p "$OUTPUT_DIR"

# CSV header
echo "file,before_cov,before_ops,after_cov,after_ops" > "$SUMMARY_CSV"

# Collect all JSON files except manifest
FILES=($(ls "$INPUT_DIR"/*.json | grep -v manifest | sort))
TOTAL=${#FILES[@]}

echo "========================================"
echo " CIM-ATPG Generator Batch Run"
echo " Total files: $TOTAL"
echo " Output dir:  $OUTPUT_DIR"
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

    # Run generator
    OUTPUT=$($BINARY --mode generate "$INPUT_FILE" "$OUT_JSON" "$OUT_HTML" \
        --max-slots 4 --max-L 6 2>&1) || true

    # Parse before-refine coverage
    REFINE_BEFORE=$(echo "$OUTPUT" | grep -oP '\[Refine\] Before: \K[0-9.]+' || echo "")
    REFINE_AFTER=$(echo "$OUTPUT" | grep -oP '\[Refine\] After:\s+\K[0-9.]+' || echo "")

    # If refine was skipped, before=after=best sweep coverage
    if [ -z "$REFINE_BEFORE" ]; then
        BEST_COV=$(echo "$OUTPUT" | grep -oP '\[Sweep\] Best coverage: \K[0-9.]+' || echo "0")
        REFINE_BEFORE="$BEST_COV"
        REFINE_AFTER="$BEST_COV"
    fi

    # Get before-refine ops: the best sweep line's ops
    # Find the sweep line with the highest cov (last one with max cov)
    BEFORE_OPS=$(echo "$OUTPUT" | grep -oP '\[Sweep\].*\(ops=\K[0-9]+(?=\).*-> cov='"$REFINE_BEFORE"')' | tail -1 || echo "")
    if [ -z "$BEFORE_OPS" ]; then
        # Fallback: get from the Refine line "Greedy best slots=X" => ops = best sweep's ops
        BEST_SLOTS=$(echo "$OUTPUT" | grep -oP '\[Refine\] Greedy best slots=\K[0-9]+' || echo "")
        if [ -n "$BEST_SLOTS" ]; then
            # Find the sweep line with that slots value and highest cov
            BEFORE_OPS=$(echo "$OUTPUT" | grep -P "\[Sweep\].*slots=$BEST_SLOTS," | grep -oP '\(ops=\K[0-9]+' | tail -1 || echo "N/A")
        else
            # Use Report ops as fallback (no refine happened)
            BEFORE_OPS=$(echo "$OUTPUT" | grep -oP '\[Report\].*\(ops=\K[0-9]+' || echo "N/A")
        fi
    fi

    # Get after-refine ops from Report line
    AFTER_OPS=$(echo "$OUTPUT" | grep -oP '\[Report\].*\(ops=\K[0-9]+' || echo "N/A")

    echo "before=${REFINE_BEFORE}%/${BEFORE_OPS}ops -> after=${REFINE_AFTER}%/${AFTER_OPS}ops"
    echo "$FNAME,$REFINE_BEFORE,$BEFORE_OPS,$REFINE_AFTER,$AFTER_OPS" >> "$SUMMARY_CSV"
done

echo ""
echo "========================================"
echo " All $TOTAL tests completed!"
echo " Summary: $SUMMARY_CSV"
echo "========================================"
