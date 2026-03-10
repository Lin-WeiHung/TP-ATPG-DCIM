#!/usr/bin/env bash
# run_remaining_tests.sh - Continue from where run_all_tests.sh left off
set -euo pipefail
cd "$(dirname "$0")/.."

BINARY="./cim-atpg"
MANIFEST="input_list/manifest.json"
RESULTS_CSV="output/sweep_results.csv"
RESULTS_DIR="output/sweep_runs"

mkdir -p "$RESULTS_DIR"

# Get list of already-done files
DONE_FILES=$(tail -n +2 "$RESULTS_CSV" | cut -d',' -f1 || true)

TOTAL=$(python3 -c "import json; m=json.load(open('$MANIFEST')); print(len(m))")

IDX=0
python3 -c "
import json
m = json.load(open('$MANIFEST'))
for entry in m:
    print(entry['file'] + '|' + str(entry['remaining_count']) + '|' + entry['desc'])
" | while IFS='|' read -r FNAME NFAULTS DESC; do
    IDX=$((IDX + 1))

    # Skip already done
    if echo "$DONE_FILES" | grep -qF "$FNAME"; then
        echo "[$IDX/$TOTAL] $FNAME -- SKIP (already done)"
        continue
    fi

    INPUT_FILE="input_list/$FNAME"
    BASE=$(basename "$FNAME" .json)
    OUT_JSON="$RESULTS_DIR/${BASE}_result.json"
    OUT_HTML="$RESULTS_DIR/${BASE}_result.html"

    echo -n "[$IDX/$TOTAL] $FNAME ($NFAULTS faults) ... "

    OUTPUT=$($BINARY --mode generate "$INPUT_FILE" "$OUT_JSON" "$OUT_HTML" \
        --max-slots 4 --max-L 6 2>&1) || true

    BEST_COV=$(echo "$OUTPUT" | grep -oP 'Best coverage: \K[0-9.]+' || echo "0")
    BEST_CFG=$(echo "$OUTPUT" | grep -oP 'Best config: \K\S+' || echo "N/A")
    REACHED=$(echo "$OUTPUT" | grep -oP 'Reached 100%: \K\S+' || echo "No")
    ELAPSED=$(echo "$OUTPUT" | grep -oP 'Total elapsed: \K[0-9]+' || echo "0")

    echo "cov=${BEST_COV}% config=${BEST_CFG} 100%=${REACHED} (${ELAPSED}ms)"
    echo "$FNAME,$NFAULTS,$BEST_COV,$BEST_CFG,$REACHED,$ELAPSED" >> "$RESULTS_CSV"
done

echo ""
echo "[Runner] All done! Results in $RESULTS_CSV"
