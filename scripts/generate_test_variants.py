#!/usr/bin/env python3
"""
Generate multiple variants of S_C_faults.json by systematically removing faults.
Strategies:
  1. Leave-one-out: remove 1 fault at a time (33 variants)
  2. By category: remove all faults of one category (3 variants)
  3. By cell_scope: remove all faults of one scope (4 variants)
  4. By fault family: remove related fault groups (e.g., all CFid, all CID*, all IC*, etc.)
"""
import json, os, re, copy
from collections import defaultdict

INPUT = os.path.join(os.path.dirname(__file__), '..', 'input', 'S_C_faults.json')
OUTDIR = os.path.join(os.path.dirname(__file__), '..', 'input_list')

with open(INPUT) as f:
    full_faults = json.load(f)

os.makedirs(OUTDIR, exist_ok=True)

manifest = []  # list of (filename, description, removed_ids)

def write_variant(name: str, desc: str, faults: list, removed_ids: list):
    path = os.path.join(OUTDIR, name)
    with open(path, 'w') as f:
        json.dump(faults, f, indent=2, ensure_ascii=False)
    manifest.append({"file": name, "desc": desc, "removed": removed_ids,
                     "remaining_count": len(faults)})

# ── 0. Full (baseline) ──
write_variant("v00_full.json", "Full (baseline)", full_faults, [])

# ── 1. Leave-one-out ──
for i, fault in enumerate(full_faults):
    fid = fault["fault_id"]
    subset = [f for j, f in enumerate(full_faults) if j != i]
    name = f"v01_drop_{fid.replace('(', '_').replace(')', '_').replace(',', '_').replace('↓', 'dn').replace('↑', 'up')}.json"
    # sanitize filename further
    name = re.sub(r'[^a-zA-Z0-9_.\-]', '', name)
    write_variant(name, f"Remove {fid}", subset, [fid])

# ── 2. By category ──
categories = sorted(set(f["category"] for f in full_faults))
for cat in categories:
    subset = [f for f in full_faults if f["category"] != cat]
    removed = [f["fault_id"] for f in full_faults if f["category"] == cat]
    name = f"v02_no_{cat}.json"
    write_variant(name, f"Remove category={cat}", subset, removed)

# ── 3. By cell_scope ──
scopes = sorted(set(f["cell_scope"] for f in full_faults))
for scope in scopes:
    subset = [f for f in full_faults if f["cell_scope"] != scope]
    removed = [f["fault_id"] for f in full_faults if f["cell_scope"] == scope]
    safe = scope.replace(' ', '_').replace('(', '').replace(')', '')
    name = f"v03_no_{safe}.json"
    write_variant(name, f"Remove scope={scope}", subset, removed)

# ── 4. By fault family (prefix grouping) ──
families = defaultdict(list)
for f in full_faults:
    fid = f["fault_id"]
    # Extract family prefix: SA, TF, CFid, CIDWD, CIDDB, CIDWDB, CIDD, CDCFst, IC, SDC, CIDC, CI, DDCB, CIDCB
    m = re.match(r'^(SA|TF[ud]?|CFid|CIDWD|CIDDB|CIDWDB|CIDD|CDCFst|IC|SDC|CIDC(?!B)|CIDCB|CI(?!D)|DDCB)', fid)
    if m:
        families[m.group(1)].append(f)
    else:
        families["other"].append(f)

for family, faults_in_family in sorted(families.items()):
    if len(faults_in_family) < 2:
        continue  # skip single-fault families (already covered by leave-one-out)
    removed_ids_set = {f["fault_id"] for f in faults_in_family}
    subset = [f for f in full_faults if f["fault_id"] not in removed_ids_set]
    removed = [f["fault_id"] for f in faults_in_family]
    safe = family.replace('(', '').replace(')', '')
    name = f"v04_no_family_{safe}.json"
    write_variant(name, f"Remove family={family} ({len(faults_in_family)} faults)", subset, removed)

# ── 5. Combined: remove all "must_compute" + specific heavy families ──
# Remove all compute-only faults
subset = [f for f in full_faults if f["category"] != "must_compute"]
removed = [f["fault_id"] for f in full_faults if f["category"] == "must_compute"]
# (already done in category, skip)

# Remove all two-cell faults
subset = [f for f in full_faults if "two cell" not in f["cell_scope"]]
removed = [f["fault_id"] for f in full_faults if "two cell" in f["cell_scope"]]
write_variant("v05_single_cell_only.json", "Single cell faults only", subset, removed)

# Remove all single-cell faults
subset = [f for f in full_faults if "single cell" not in f["cell_scope"]]
removed = [f["fault_id"] for f in full_faults if "single cell" in f["cell_scope"]]
write_variant("v05_two_cell_only.json", "Two-cell faults only", subset, removed)

# ── Write manifest ──
manifest_path = os.path.join(OUTDIR, "manifest.json")
with open(manifest_path, 'w') as f:
    json.dump(manifest, f, indent=2, ensure_ascii=False)

print(f"Generated {len(manifest)} variant files in {OUTDIR}")
for m in manifest:
    print(f"  {m['file']:50s}  ({m['remaining_count']:2d} faults)  {m['desc']}")
