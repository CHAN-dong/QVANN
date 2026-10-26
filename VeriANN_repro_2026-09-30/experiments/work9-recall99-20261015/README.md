# work9: common recall range through 99%

This study replaces only the three SIFT1M recall-versus-cost curves in work9.xlsx.
Other overall, scale, and k experiments retain their previous measurements.

All methods use SIFT1M, k=10, query IDs 100..199, M=8, efConstruction=100,
single-thread search and verification. The target recall thresholds are
60%, 65%, 70%, 75%, 80%, 85%, 90%, 95%, 97%, 98%, 99%.
ANNProof varies efSearch; VLAH varies probes at fixed efSearch=100;
Ours uses the validated LUT variant and varies expansion budget E.

Parameter scans use the same 100-query workload to locate thresholds. Selected
operating points are measured again with three repeats (300 measurements each).
These are matched workload operating points, not independent held-out recall
estimates. Each point uses the smallest tested parameter reaching the threshold;
the scan does not establish a global cost optimum. Actual recall is retained in
all tables and charts; no synthetic zero points, interpolation, or extrapolation.
Discrete parameters can cause small differences in the actual starting recall.

Query time includes search, exact candidate reranking as applicable, and actual
VO generation/serialization. Ours LUT preparation is inside the measured code.
Verify time includes VO decoding, authentication, replay and relevant result
checks. VO KB = serialized bytes / 1000. Network and blockchain time is excluded.
Baselines are algorithmic reimplementations with their previously disclosed
scope; no claim of strict author-code or full blockchain reproduction is made.

Run sequentially under WSL from the project root:

```sh
python3 scripts/work9_recall99.py
python3 scripts/fill_work9_recall99.py
python3 scripts/verify_work9_recall99.py
```

Raw results are under `results/work9-recall99-20261015/`. Pilot and evaluation
logs and parameter selection are stored here. `work9_before_recall99.xlsx` is
the original backup; `work9_completed_recall99.xlsx` is the completed copy.
The exporter preserves concurrent workbook edits, maintains main-table cell
provenance, marks previous recall-curve configuration rows as historical, and
adds `Recall99 measurements` with current measurements and three scatter charts.
`final_validation.json` audits cells against raw CSVs, source fingerprints,
dataset identity, honest verification, mutation rejection, and wire byte sizes.
