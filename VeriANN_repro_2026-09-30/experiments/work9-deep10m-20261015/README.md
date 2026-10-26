# DEEP scale extension to 10M

User-authorized extension: replace only the DEEP worksheet's 5M and 10M
dataset-size results with measured ANNProof, VLAH and optimized VeriANN results.
The original 100K/500K/1M scale points and all other experiments are retained.

Data: official DEEP1B first 10M float32 vectors, 96 dimensions, original L2
metric. The converted first 1M must match the existing DEEP1M file exactly.
The official 10K public query source is unchanged. Exact ground truth is
computed separately for each requested database prefix by the benchmarks.

Protocol: k=10, query IDs 100..119, 20 independent queries, three timing repeats,
M=16, efConstruction=200. Graph construction uses 16 threads; search, actual
VO serialization, and client authentication/replay are single-threaded.
Query time includes search and VO generation. Verification includes decoding,
authentication and replay. VO KB means bytes/1000. Setup/index-building time
is excluded from per-query timings and retained in the original logs.

Operating points target at least approximately 95% recall on the same workload.
Search-only calibration values are never substituted for full protocol timings
or VO sizes. Coarse-to-fine bucket probing avoids redundant exhaustive grids at
250/500 buckets. Global graph parameters are refined if coarse grids overshoot
95% by more than one percentage point. The full protocol is compared against
the chosen pilot point for every query/repeat using recall, visited and expanded
counts. Actual recalls and selected parameters are recorded in cell comments.
This is a small-query preview and is not held-out recall estimation.

VLAH retains the exact DEEP1M LSH width across all scales, so the earlier and new
size points use the same data-derived routing setting. ANNProof and Ours reuse
the same cached HNSW graph for a given database prefix. Existing protocol source
and optimized LUT binary are unchanged. Baselines keep the previously disclosed
algorithmic-reimplementation and offline commitment scope.

The initial workbook is backed up before edits. The result workbook is saved
after each passed run; original-file writes check its hash to preserve concurrent
edits. The new auxiliary sheet includes actual recalls, parameters, CSV sources
and the three full size-versus-cost charts.

If the workbook is saved externally, its stable latest version is backed up and
used as the merge baseline after checking the DEEP scale-column locations. This
preserves user sheet renaming/reordering and unrelated edits. The task-start
snapshot remains `work9_before_deep10m.xlsx`; the preservation check uses
`work9_merge_baseline.xlsx` when such a save occurred.

Run from the project root in WSL:

```sh
python3 scripts/prepare_deep10m.py
python3 scripts/run_work9_deep10m.py
```

The earlier multidataset study's completed workbook and validation remain
historical snapshots from before this extension. Rerun the extension after any
older workbook generator that would restore the former not-applicable cells.
