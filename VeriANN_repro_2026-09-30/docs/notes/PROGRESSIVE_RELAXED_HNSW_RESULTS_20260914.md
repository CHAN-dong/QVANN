# SQ4 to SQ6 progressive refinement with aggregated MAC

SIFT100K, 100 queries, k=10, fixed beam=5 and rounds=12. The MAC response contains
four 16-byte aggregates for SQ4, delta-4-to-6, adjacency, and raw records.

| mode | recall | expanded | disclosed | SQ4-only | refined SQ6 | raw | total KiB |
|---|---:|---:|---:|---:|---:|---:|---:|
| SQ4 routing | 0.989 | 62.99 | 824.11 | 689.35 | 134.76 | 22.76 | 76.78 |
| shortlist2 | 0.989 | 62.96 | 828.75 | 679.84 | 148.91 | 22.71 | 77.57 |
| shortlist4 | 0.991 | 62.99 | 832.48 | 662.72 | 169.76 | 22.74 | 78.58 |
| certified | 0.991 | 62.99 | 833.23 | 174.57 | 658.66 | 22.74 | 95.82 |

Certified refinement uses conservative SQ4 lower/upper intervals and exactly matches
all 100 full-SQ6 traces. Because these 128-dimensional intervals overlap heavily, it
refines 79.05% of disclosed nodes and saves only 2.90% versus 98.69 KiB.

Shortlist4 refines the four-beam SQ4 shortlist before choosing each SQ6 beam. It is a
heuristic rather than a proof-safe SQ4 exclusion rule and does not preserve every SQ6
trace, but restores Recall 0.991 empirically. It leaves 662.72 nodes at SQ4 and reduces
total VO by 20.37% to 78.58 KiB while raw openings remain 22.74.
