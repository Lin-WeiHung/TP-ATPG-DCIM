#!/usr/bin/env python3
"""Analyze sweep results and produce a summary report."""
import csv, json, os

CSV_PATH = os.path.join(os.path.dirname(__file__), '..', 'output', 'sweep_results.csv')
MANIFEST_PATH = os.path.join(os.path.dirname(__file__), '..', 'input_list', 'manifest.json')

with open(MANIFEST_PATH) as f:
    manifest = {m['file']: m for m in json.load(f)}

rows = []
with open(CSV_PATH) as f:
    reader = csv.DictReader(f)
    for r in reader:
        r['best_coverage'] = float(r['best_coverage'])
        r['remaining_faults'] = int(r['remaining_faults'])
        r['elapsed_ms'] = int(r['elapsed_ms'])
        rows.append(r)

print("=" * 90)
print("CIM ATPG Fault Removal Impact Analysis")
print("=" * 90)

# Separate leave-one-out (v01) from others
baseline = [r for r in rows if r['file'] == 'v00_full.json'][0]
loo = [r for r in rows if r['file'].startswith('v01_')]
cat_tests = [r for r in rows if r['file'].startswith('v02_')]
scope_tests = [r for r in rows if r['file'].startswith('v03_')]
family_tests = [r for r in rows if r['file'].startswith('v04_')]
combo_tests = [r for r in rows if r['file'].startswith('v05_')]

print(f"\nBaseline: {baseline['best_coverage']}% coverage ({baseline['remaining_faults']} faults)")

# ── Leave-one-out analysis ──
print("\n" + "─" * 90)
print("1. LEAVE-ONE-OUT ANALYSIS (移除單一 fault 的影響)")
print("─" * 90)

loo_impact = [r for r in loo if r['best_coverage'] < 100.0]
loo_safe = [r for r in loo if r['best_coverage'] >= 100.0]

print(f"\n  移除後仍 100%（無影響）: {len(loo_safe)} 個 fault")
print(f"  移除後 < 100%（有影響）: {len(loo_impact)} 個 fault")

if loo_impact:
    print(f"\n  {'Fault ID':<25} {'Coverage':>10} {'Best Config':<20} {'Description':<30}")
    print(f"  {'─'*25} {'─'*10} {'─'*20} {'─'*30}")
    for r in sorted(loo_impact, key=lambda x: x['best_coverage']):
        m = manifest.get(r['file'], {})
        removed = m.get('removed', ['?'])[0] if m else '?'
        print(f"  {removed:<25} {r['best_coverage']:>9.4f}% {r['best_config']:<20}")

print(f"\n  無影響的 fault:")
for r in loo_safe:
    m = manifest.get(r['file'], {})
    removed = m.get('removed', ['?'])[0] if m else '?'
    print(f"    ✓ {removed}")

# ── Category analysis ──
print("\n" + "─" * 90)
print("2. BY CATEGORY (移除整個 category)")
print("─" * 90)
for r in cat_tests:
    m = manifest.get(r['file'], {})
    status = "✓ 100%" if r['best_coverage'] >= 100.0 else f"✗ {r['best_coverage']:.4f}%"
    print(f"  {r['file']:<45} {status:<15} removed: {len(m.get('removed', []))} faults")

# ── Scope analysis ──
print("\n" + "─" * 90)
print("3. BY CELL SCOPE (移除整個 scope 類型)")
print("─" * 90)
for r in scope_tests:
    m = manifest.get(r['file'], {})
    status = "✓ 100%" if r['best_coverage'] >= 100.0 else f"✗ {r['best_coverage']:.4f}%"
    print(f"  {r['file']:<45} {status:<15} removed: {len(m.get('removed', []))} faults")

# ── Family analysis ──
print("\n" + "─" * 90)
print("4. BY FAULT FAMILY (移除同族群 fault)")
print("─" * 90)
for r in sorted(family_tests, key=lambda x: x['best_coverage']):
    m = manifest.get(r['file'], {})
    status = "✓ 100%" if r['best_coverage'] >= 100.0 else f"✗ {r['best_coverage']:.4f}%"
    removed_ids = m.get('removed', [])
    print(f"  {r['file']:<45} {status:<15} removed: {', '.join(removed_ids)}")

# ── Combo analysis ──
print("\n" + "─" * 90)
print("5. COMBO TESTS")
print("─" * 90)
for r in combo_tests:
    m = manifest.get(r['file'], {})
    status = "✓ 100%" if r['best_coverage'] >= 100.0 else f"✗ {r['best_coverage']:.4f}%"
    print(f"  {r['file']:<45} {status:<15} ({r['remaining_faults']} faults remain)")

# ── Root cause analysis ──
print("\n" + "=" * 90)
print("6. ROOT CAUSE ANALYSIS（原因分析）")
print("=" * 90)

critical_faults = [manifest.get(r['file'], {}).get('removed', ['?'])[0] for r in loo_impact]
print(f"""
關鍵發現：移除以下 fault 會導致覆蓋率下降:
  {', '.join(sorted(critical_faults))}

這些 fault 的共同特點分析：
""")

# Categorize the critical faults
from collections import defaultdict
crit_by_cat = defaultdict(list)
crit_by_scope = defaultdict(list)

faults_path = os.path.join(os.path.dirname(__file__), '..', 'input', 'S_C_faults.json')
with open(faults_path) as f:
    all_faults = json.load(f)
faults_dict = {f['fault_id']: f for f in all_faults}

for fid in critical_faults:
    if fid in faults_dict:
        fd = faults_dict[fid]
        crit_by_cat[fd['category']].append(fid)
        crit_by_scope[fd['cell_scope']].append(fid)

print("  按 category 分佈:")
for cat, fids in sorted(crit_by_cat.items()):
    total_in_cat = sum(1 for f in all_faults if f['category'] == cat)
    print(f"    {cat}: {len(fids)}/{total_in_cat} 個是關鍵 → {', '.join(fids)}")

print("\n  按 cell_scope 分佈:")
for scope, fids in sorted(crit_by_scope.items()):
    total_in_scope = sum(1 for f in all_faults if f['cell_scope'] == scope)
    print(f"    {scope}: {len(fids)}/{total_in_scope} 個是關鍵 → {', '.join(fids)}")

# Check primitives of critical faults
print("\n  關鍵 fault 的 primitive 特徵:")
for fid in sorted(critical_faults):
    if fid in faults_dict:
        prims = faults_dict[fid]['fault_primitives']
        print(f"    {fid:<25} {prims}")

print(f"""
原因總結:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

1. Greedy 路徑依賴（主因）:
   移除某個 fault 後，Greedy 在每步的「最多覆蓋」評分改變，
   導致選擇了不同的 march element 序列。原本能「搭便車」覆蓋
   其他 fault 的 element 不再被選中。

2. 關鍵 fault 提供獨特的 TP (Test Primitive):
   這些 fault 產生的 TP 需要特定的 op 組合（如 Ci+D 的交互），
   當這些 fault 不在時，對應的 op 組合不會被 Greedy 選中，
   但這些 op 組合恰好也是「間接」覆蓋其他 fault 的關鍵。

3. 對稱性破壞:
   CFid(↑,0)/(↑,1) 移除會影響但 CFid(↓,0)/(↓,1) 不會，
   說明 ↑ 類的 TP 產生了特殊的 op 需求，引導 Greedy 選出
   能同時覆蓋更多 fault 的 element。

4. 搜尋空間不足:
   部分組態的 best_config 是 slots4_L5 而非 slots4_L6，
   表示 Greedy 在較小空間就停止了，可能需要更大的 L 才能補回。
""")
