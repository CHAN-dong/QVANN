# Relaxed HNSW / Expand-or-Harvest (SIFT100K)

Date: 2026-09-14

## Protocol

The original cached HNSW topology is unchanged. Routing never reads raw vectors or exact
distances. Upper-layer descent uses bounded SQ midpoint ordering. At level 0, each of
the fixed rounds expands the best `beam_width` unexpanded nodes according to the
authenticated SQ code. Every disclosed neighbor is harvested into candidate set C,
including nodes not selected for later expansion.

After routing, the server computes exact top-10 inside C and obtains tau. Verification
uses conservative SQ4 then SQ6 lower bounds; raw vectors are opened only where SQ6
cannot prove distance greater than tau (including returned results).

VO uses separated, aligned Merkle trees for routing codes, adjacency records, SQ6
refinement, and raw vectors. Routing codes are reused during candidate verification;
a raw opening replaces, rather than duplicates, the SQ6 refinement payload.

## Key 100-query results

| routing | beam x rounds | Recall@10 | expanded | discovered C | harvested unexpanded | raw | routing VO KiB | verification VO KiB | total KiB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SQ6 | 2 x 8 | 0.860 | 19.63 | 310.61 | 290.98 | 20.03 | 107.25 | 16.44 | 123.69 |
| SQ6 | 3 x 10 | 0.945 | 33.39 | 489.67 | 456.28 | 21.90 | 158.55 | 17.89 | 176.44 |
| SQ6 | 4 x 8 | 0.951 | 35.18 | 520.39 | 485.21 | 21.97 | 166.94 | 17.95 | 184.89 |
| SQ4 | 7 x 8 | 0.987 | 58.78 | 799.99 | 741.21 | 22.62 | 216.60 | 55.56 | 272.16 |
| **SQ6** | **5 x 12** | **0.991** | **62.99** | **833.23** | **770.24** | **22.74** | **250.56** | **18.56** | **269.12** |
| SQ6 | 4 x 16 | 0.992 | 67.17 | 865.86 | 798.69 | 22.81 | 259.16 | 18.62 | 277.78 |
| SQ6 | 8 x 12 | 0.998 | 98.60 | 1193.24 | 1094.64 | 22.90 | 341.41 | 18.68 | 360.09 |

The best point at Recall about 0.99 is SQ6 beam=5, rounds=12. Its total is 269.12 KiB,
4.49% below the prior 281.76 KiB/query result, and raw vectors remain at 22.74/query.
However, the requested 20--35 expansions only achieves Recall 0.86--0.951.

## Answer to the topology question

The experiment supports a qualified yes: HNSW topology supplies most of the high
Recall, because approximate SQ-only ordering reaches Recall 0.991 without any exact
routing distance. But exact heap ordering is important for routing efficiency. Removing
it raises the expansion requirement from about 58 to about 63 and the discovered set
from about 755 to about 833 at comparable Recall. Under a hard 20--35 expansion budget,
Recall falls well below 0.99.

The bottleneck is therefore routing disclosure, not final-tau verification: at the best
point routing costs 250.56 KiB while candidate verification costs only 18.56 KiB.
Increasing C to 1000--1500 by additional graph expansion is counterproductive even
though raw openings stay nearly flat around 23.

## Reproduction

```bash
make relaxed_hnsw_harvest_experiment
./relaxed_hnsw_harvest_experiment --queries 100 --beams 3,5,6,7 \
  --rounds 8,10,12,14 --bits 4,6 \
  --output relaxed_hnsw_harvest_refine_q100.csv
```

Full sweeps are in `relaxed_hnsw_harvest_q100.csv` and
`relaxed_hnsw_harvest_refine_q100.csv`.
