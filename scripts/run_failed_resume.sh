#!/usr/bin/env bash
# run_failed_resume.sh - Run failed cases, skip already-done ones
set -euo pipefail
cd "$(dirname "$0")/.."

BINARY="./cim-atpg"
RESULTS_CSV="output/multistart_results.csv"
RESULTS_DIR="output/multistart_runs"

mkdir -p "$RESULTS_DIR"

# Create header if file doesn't exist
if [[ ! -f "$RESULTS_CSV" ]]; then
    echo "file,remaining_faults,old_coverage,new_coverage,best_config,reached_100,elapsed_ms" > "$RESULTS_CSV"
fi

# Get already done
DONE_FILES=$(tail -n +2 "$RESULTS_CSV" 2>/dev/null | cut -d',' -f1 || true)

declare -A FAILED_CASES
FAILED_CASES=(
    ["v01_drop_CFid_up_0_.json"]="96.9697"
    ["v01_drop_CFid_up_1_.json"]="96.9697"
    ["v01_drop_CIDDB_1_1_.json"]="96.9697"
    ["v01_drop_CIDWDB.json"]="96.9697"
    ["v01_drop_CIDD_0_0_.json"]="95.4545"
    ["v01_drop_CIDD_1_0_.json"]="95.4545"
    ["v01_drop_IC00.json"]="95.4545"
    ["v01_drop_CIDCB_0_00_.json"]="95.4545"
    ["v01_drop_CIDCB_1_11_.json"]="93.9394"
    ["v01_drop_CI_10_11_.json"]="98.4848"
    ["v02_no_either_read_or_compute.json"]="95.0000"
    ["v03_no_single_cell.json"]="94.4444"
    ["v03_no_two_cell_cross_row.json"]="97.8261"
    ["v03_no_two_cell_same_row.json"]="98.4848"
    ["v04_no_family_CFid.json"]="95.0000"
    ["v04_no_family_CI.json"]="98.3871"
    ["v04_no_family_CIDCB.json"]="95.3125"
    ["v04_no_family_CIDD.json"]="93.3333"
    ["v04_no_family_CIDWD.json"]="96.8750"
    ["v04_no_family_DDCB.json"]="98.4375"
    ["v04_no_family_IC.json"]="95.0000"
    ["v05_two_cell_only.json"]="94.4444"
)

TOTAL=${#FAILED_CASES[@]}
IDX=0

for FNAME in $(echo "${!FAILED_CASES[@]}" | tr ' ' '\n' | sort); do
    IDX=$((IDX + 1))
    
    # Skip already done
    if echo "$DONE_FILES" | grep -qF "$FNAME"; then
        echo "[$IDX/$TOTAL] $FNAME -- SKIP (already done)"
        continue
    fi

    OLD_COV="${FAILED_CASES[$FNAME]}"
    INPUT_FILE="input_list/$FNAME"
    BASE=$(basename "$FNAME" .json)
    OUT_JSON="$RESULTS_DIR/${BASE}_result.json"
    OUT_HTML="$RESULTS_DIR/${BASE}_result.html"

    NFAULTS=$(python3 -c "import json; print(len(json.load(open('$INPUT_FILE'))))")

    echo -n "[$IDX/$TOTAL] $FNAME ($NFAULTS faults, was ${OLD_COV}%) ... "

    OUTPUT=$($BINARY --mode generate "$INPUT_FILE" "$OUT_JSON" "$OUT_HTML" \
        --max-slots 4 --max-L 6 2>&1) || true

    BEST_COV=$(echo "$OUTPUT" | grep -oP 'Best coverage: \K[0-9.]+' || echo "0")
    BEST_CFG=$(echo "$OUTPUT" | grep -oP 'Best config: \K\S+' || echo "N/A")
    REACHED=$(echo "$OUTPUT" | grep -oP 'Reached 100%: \K\S+' || echo "No")
    ELAPSED=$(echo "$OUTPUT" | grep -oP 'Total elapsed: \K[0-9]+' || echo "0")

    if [[ "$REACHED" == "Yes" ]]; then
        DELTA="FIXED ✓"
    else
        DELTA=$(python3 -c "print(f'{float(\"$BEST_COV\") - float(\"$OLD_COV\"):+.4f}%')")
    fi

    echo "cov=${BEST_COV}% (${DELTA}) config=${BEST_CFG} (${ELAPSED}ms)"
    echo "$FNAME,$NFAULTS,$OLD_COV,$BEST_COV,$BEST_CFG,$REACHED,$ELAPSED" >> "$RESULTS_CSV"
done

echo ""
echo "═══════════ FINAL SUMMARY ═══════════"
tail -n +2 "$RESULTS_CSV" | while IFS=',' read -r F NF OLD NEW CFG R100 MS; do
    if [[ "$R100" == "Yes" ]]; then
        STATUS="✓ FIXED"
    else
        DIFF=$(python3 -c "print(f'{float(\"$NEW\") - float(\"$OLD\"):+.4f}')")
        STATUS="✗ ${NEW}% (${DIFF})"
    fi
    printf "  %-42s was %9s%% → %s\n" "$F" "$OLD" "$STATUS"
done
FIXED=$(tail -n +2 "$RESULTS_CSV" | grep -c ",Yes," || echo "0")
echo ""
echo "  Fixed: $FIXED / $TOTAL"
