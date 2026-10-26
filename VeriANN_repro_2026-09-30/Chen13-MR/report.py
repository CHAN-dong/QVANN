"""Export measured points and a concise experiment report after completion."""
import csv,json
from run import ROOT, OUT, STUDY, DATA
assert (STUDY/'RUN_FINISHED.json').exists()
records=[]
for f in sorted(OUT.glob('*/measurement.json')):
 r=json.loads(f.read_text())
 if r['dataset'].startswith('smoke') or r['kind']=='pilot':continue
 records.append((f,r))
columns=['dataset','N','kind','k','recall','samples','query_ms','search_ms','encode_ms','verify_ms','vo_kb','index_mb','build_seconds','snapshot_write_seconds','peak_process_rss_GiB','record']
rows=[]
for f,r in records:
 for p in r['points']:
  a=r['native'];rows.append([r['dataset'],r['N'],r['kind'],p['k'],p['recall'],p['samples'],p['query_ms'],p['search_ms'],p['encode_ms'],p['verify_ms'],p['vo_kb'],a['index_bytes']/1e6,a['build_seconds'],a['snapshot_write_seconds'],a['peak_rss_KiB']/2**20,str(f)])
with (STUDY/'measured-points.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(columns);w.writerows(rows)
lines=['# Chen13-MR high-dimensional plaintext adaptation — measured results','',
 'Implements the plaintext exact-kNN authentication in paper §5.1.1/Fig.6(a), with spatial bulk loading replacing R*-tree construction. No privacy layer or Power Diagram. These are measurements of our adaptation, not the authors\' complete confidential MR/EMR/PD system.','',
 '| Dataset | Recall@10 | Query + VO (ms) | VO (MB) | Verify (ms) | Index (MB) | Build (s) |',
 '|---|---:|---:|---:|---:|---:|---:|']
for ds,(name,n) in DATA.items():
 r=next(r for _,r in records if r['dataset']==ds and r['N']==n and r['kind']=='overall');p=next(p for p in r['points'] if p['k']==10);a=r['native']
 lines.append(f"| {name} | 100% | {p['query_ms']:.3f} | {p['vo_kb']/1000:.3f} | {p['verify_ms']:.3f} | {a['index_bytes']/1e6:.3f} | {a['build_seconds']:.3f} |")
lines+=['',f'{len(rows)} distinct measured operating points in {len(records)} fresh native runs; '+str(sum(r[5] for r in rows))+' timed queries; '+str(sum(r['native']['tamper_tests'] for _,r in records))+' rejected negative cases. Each point uses 20 query IDs × 3 repeats, one thread. Independent exhaustive ground truth is recomputed per N.','',
 'K and size sweeps are restricted to SIFT/DEEP. SIFT size sweeps use BIGANN prefixes; the main SIFT1M default is the separate standard dataset. DEEP size=1M/k=10 reuses the identical default measurement.','',
 'Only the 100% exact point belongs on a recall plot. Existing approximate-method defaults and V3DB forecasts are preserved. Do not describe these as equal-recall 95% experiments.','',
 'Index build time excludes input reading, ground-truth search and final snapshot I/O, consistent with the existing index table. Snapshot I/O and whole-process peak RSS are separately available in the CSV/workbook. No network/disk VO transfer is included in query time.','',
 'Raw records, complete serving snapshots, signed proof examples and sample CSVs: `results/work9-chen13-20261021/`. Full independent audit: `independent-audit.json`. Workbook preservation/mappings: `fill-audit.json` and the Chen13 workbook sheets.']
(STUDY/'REPORT.md').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines[:11]))
