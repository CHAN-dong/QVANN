# One-parameter E-budget Relaxed HNSW

Protocol: SQ4 upper-layer greedy descent followed by a level-0 ordered queue. Pop and
expand exactly one SQ4-nearest node at a time until the single global unique-node budget
E is exhausted. Exact top-k over all disclosed nodes determines tau, followed by
SQ4-to-SQ6-to-raw verification and four 16-byte aggregate MACs.

| mode | E | recall | expanded | disclosed | SQ4-only | refined | raw | VO KiB | compute ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SQ4 top-1 | 32 | 0.942 | 32 | 458.71 | 354.67 | 104.04 | 21.95 | 48.18 | 1.93 |
| SQ4 top-1 | 48 | 0.982 | 48 | 641.45 | 518.87 | 122.58 | 22.50 | 62.66 | 2.57 |
| SQ4 top-1 | 64 | 0.992 | 64 | 811.86 | 675.82 | 136.04 | 22.80 | 76.01 | 3.15 |
| SQ4 top-1 | 80 | 0.997 | 80 | 969.34 | 824.49 | 144.85 | 22.85 | 88.22 | 3.66 |
| top2 overlap | 64 | 0.991 | 64 | 812.25 | 676.47 | 135.78 | 22.76 | 76.01 | 3.32 |
| certified overlap | 64 | 0.992 | 64 | 817.29 | 682.11 | 135.18 | 22.78 | 76.35 | 4.09 |

The recommended point is plain SQ4 top-1 with E=64. It reaches Recall 0.992 with
76.01 KiB/query, 22.98% below the full-SQ6 MAC baseline (98.69 KiB) and 3.27% below
the earlier shortlist4 progressive result (78.58 KiB). Queue-time SQ6 does not improve
Recall or VO at E=64 and adds computation, so it is unnecessary on this workload.

The compute time excludes cryptographic MAC recomputation and includes routing,
server exact top-k over disclosed candidates, and final VO selection/accounting.
