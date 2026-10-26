# Three additional datasets in work9.xlsx

User-authorized scope: retain the original SIFT workbook sheets, copy its main
table three times, and fill VLAH, ANNProof, and optimized VeriANN measurements
on GIST1M, GloVe-100, and DEEP1M. Keep the original scale columns. The user
explicitly selected marking 5M and 10M as not applicable rather than replacing
the scale points. Current real scale measurements are 100K, 500K, and 1M.

Each new main sheet has 291 measured numeric result cells and 18 not-applicable
cells. Total expected: 873 measured cells and 54 not-applicable cells. Copied
SIFT numbers are cleared before any new measurements are filled.

Quick-preview protocol (user selected): 20 distinct query IDs 100..119, 3 repeats, single-thread
search/VO/verification, M=16, efConstruction=200. Graph construction uses 16
threads and cached graphs are retained. ANNProof and Ours share the same cached
HNSW graph for a given dataset/base size. Every formal query performs actual
VO serialization and client authentication/replay. Network/blockchain time is
excluded. Query time = search + VO generation; verification includes decoding;
VO KB = wire bytes / 1000. Ours uses the previously validated LUT variant.

The initial GIST M=8/efc=100 exploratory graph had only 866,694 nodes reachable
at level 0 and 870,260 across all levels from its entry, out of 1,000,000 nodes.
Its recall saturated near 97% even with very large ef. Those scans are preserved
under `results/.../gist1m/exploratory-M8-efc100/` and are not used as final table
values. The three new datasets therefore share M=16/efc=200 for all methods;
the original SIFT study retains its earlier configuration.

The replacement GIST M=16/efc=200 graph has 973,706 nodes reachable at level 0
and 974,208 across all levels. The reported 99% recall applies to the measured
query workload; it does not assert that the graph is fully connected.

Recall curves target 60/65/70/75/80/85/90/95/97/98/99%, retain actual measured
recall, and use workload-matched parameters selected on those same 20 queries.
This is not held-out recall estimation. Discrete parameters can map multiple
thresholds to one measured operating point. Overall and scale points target
at least approximately 95%; k-curves use k=1/5/10/20/50/100 with the overall
95% work point (baseline ef is raised to 100 if needed for k=100).

GloVe uses its full 1,183,514-vector normalized base for overall/recall/k, and
true prefixes for scale experiments. GIST and DEEP use their 1M bases.
All input files and preprocessing were verified by the dataset preparation study.
Exact ground truth is computed by the existing benchmark for each base/query
workload, not copied from a different dataset size.

VLAH LSH width cannot retain the SIFT value 256 for unit-scale vectors. Each
dataset therefore uses a documented width equal to the median L2 distance of
4096 deterministic base-only pairs divided by four. No query/ground-truth
information is used to choose width, and the width is fixed across all sizes
for that dataset. Bucket size=20000; other LSH settings retain existing defaults.
VLAH's ef is raised if all buckets at the current ef cannot reach the recall
target; its recall curve uses one fixed ef and varies the probe count.

Fast selection binaries include the exact existing search implementations but
skip VO generation and verification. Their output is used only for operating
point selection; timing and VO values always come from the unchanged full
protocol programs. A d=960 smoke test and subsequent point-wise checks compare
recall/visited/expanded against the authenticated full protocol for all three
methods. Original protocol sources/binaries are preserved.

The baselines retain their disclosed algorithmic-reimplementation scope; these
are not strict author-code or full blockchain reproductions.

Run sequentially under WSL from the project root:

```sh
python3 scripts/build_multidata_pilots.py
python3 scripts/smoke_multidata_pilots.py
python3 scripts/run_work9_multidata.py
python3 scripts/verify_work9_multidata.py
```

Passed jobs are resumed. Incomplete runs are retained for inspection rather than
mixed with new CSVs. Logs, fixed data-specific LSH widths, selected work points,
commands and source/binary fingerprints are retained in this study and raw
output directories. The workbook is checkpointed after every formal job; only
verified results replace pending cells. A full original backup is kept here.
If WPS locks the workbook or concurrent edits change its hash, the completed
copy is preserved without overwriting that external state.

Completed 100-query raw runs remain preserved. Their statistics use only the
same query IDs 100..119. Interrupted 100-query VLAH output is archived separately
and excluded. All new runs use 20 queries x 3 repeats. This is a quick preview,
not a large-query final evaluation.
