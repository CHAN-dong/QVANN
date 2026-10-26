# Four-dataset 1M controlled ablations

This study fills the six-row ablation tables in the existing workbook. All four
datasets use exactly their first 1,000,000 vectors, including GloVe-100.
These are project-local implementations, not new measurements of ANNProof/VLAH.

| Row | Search | Authentication |
| --- | --- | --- |
| Merkle+IVF | Exact distances over selected complete IVF lists | Raw vertex Merkle leaves; list membership and centroid commitment |
| Merkle+HNSW | Exact-distance HNSW beam search | Raw vertex + full adjacency Merkle leaves |
| Merkle+IVF+(i) | Same IVF lists and answers; SQ4/SQ6 bounds avoid some raw distances | SQ4 leaves plus demanded SQ6 deltas/raw openings; full list membership |
| Merkle+HNSW+(i) | Same HNSW traversal and answers; dynamic-threshold progressive distances | SQ4 + full adjacency leaves plus demanded deltas/raw |
| (i)+(ii) | SQ4 top1 budgeted traversal, exact rerank, then final-threshold refinement | SQ4 + full adjacency leaves for every discovered node |
| (i)+(ii)+(iii) | Exactly the same traversal and answers as previous row | Existing expansion-centric optimized protocol |

The user explicitly confirmed that the table's NSW refers to the current HNSW.
Row labels therefore say HNSW. For each dataset, rows 1/3 share nprobe, rows 2/4
share efSearch, and rows 5/6 share E. Paired answers, visited counts and expanded
counts must match exactly; selection targets approximately 95% Recall@10. The
three search families may need different parameters. SQ4 traversal is not the
same search algorithm as exact HNSW; this distinction is part of item (ii).

Common configuration: M=16, efConstruction=200, SHA-256, k=10, query IDs 100..119,
20 distinct queries with 3 timed repeats, one query thread, 16 construction
threads. The original SIFT main tables used different graph parameters and are
not reused as ablation timing measurements. IVF uses 256 lists, 65,536 evenly
spaced database-only training samples, 12 kmeans iterations and seed 42.
GloVe vectors were already normalized; squared L2 preserves cosine ranking.
The existing GIST graph's entry reaches 974,208 / 1,000,000 vertices across all
levels (see the earlier study's `gist-M16-efc200-reachability.txt`); it is not
fully entry-reachable. All graph ablations reuse that same cached graph, and
ground truth still covers the entire 1M database. The reported recall is the
actual quality of the selected 20 queries, not a graph-wide recall guarantee.
IVF vector/refinement leaves use database-ID order; graph vertex/refinement
leaves use BFS order. Each controlled pair uses identical leaf ordering.
Thus index-family comparisons include their stated layout choices; the study
does not claim an optimal authenticated IVF implementation.

Query time is search plus complete wire VO construction/serialization.
Verification includes decoding, hashing/Merkle proof checking, traversal replay
and top-k checking. It has no access to the server graph or base vectors.
All disclosed memberships, vector/code payloads and proof digests count in the
actual serialized VO. Time is ms, VO size is decimal KB, Recall@10 is in [0,1].
Loading, ground truth, index/ADS construction, transport and consensus are not
part of the query/verification timing. Resource files include setup costs.

`pilot.csv` is only for selecting operating points. Never use pilot durations
as table timings. Calibration and reported recall use the same 20 query IDs;
this is a workload preview, not held-out evaluation. Every formal VO must pass
verification. Each dataset/variant also has mutation/omission tests and a saved
wire sample. Timings can fluctuate, and an optimization need not improve every
metric on every dataset. No values are interpolated or copied between rows.

Reproduce in WSL Ubuntu from the repository root:

```sh
python3 scripts/build_work9_ablation.py
python3 scripts/run_work9_ablation.py --smoke
python3 scripts/run_work9_ablation.py
```

The runner resumes only completed runs with matching binary/source hashes.
An incomplete output directory requires inspection before retrying. Before
each workbook write it snapshots the current original, changes only completed
ablation rows and their units/HNSW labels, and verifies every other cell value.
Other user edits, dataset tables and the DEEP 5M/10M extension are preserved.
An initial backup and a completed copy are retained in this study directory.

Raw formal logs are in `results/work9-ablation-20261015/<dataset>/formal-*`.
`per_query.csv` contains split timing, counts and quality; `answers.csv` records
actual top-k IDs. Each pair has `pair_audit.json`. The final workbook includes
Chinese explanation, full measurement records and per-cell source mappings.
`final_validation.json` is produced only after all 24 rows / 96 cells pass.
The shared CSV writer's `kth_distance_ratio` and `ads_bytes` fields are reserved
zero placeholders in this executable, not measurements; they are not used in
the workbook. Actual process RSS/setup duration is in the resource logs.
