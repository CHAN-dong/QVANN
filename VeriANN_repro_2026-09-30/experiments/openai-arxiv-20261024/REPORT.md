# OpenAI-ArXiv-1M: measured overall, recall-cost curves and Table4

Source: Big ANN OpenAI-ArXiv, first1,000,000 base rows,1536 original float32 dimensions, all20,000 source query vectors. No normalization or dimension reduction. Six independently re-downloaded source ranges match prepared vectors byte-for-byte; none of the20 measurement queries exactly equals a selected base vector. See dataset-audit.json and dataset provenance.

Query IDs100..119,20 queries ×3 repetitions; k10; query/proof/verification one thread. M16/efConstruction200 shared global graph for ANNProof, QVANN and graph ablations. VLAH has separate native LSH bucket graphs. Exact exhaustive L2 ground truth is recomputed for the1M prefix. Calibration and measurement use the same20 IDs, so reported recall is not a held-out accuracy claim. All data below are unscaled native measurements; KB=1000bytes, not KiB.

## Overall raw measurements

| Method | Status | R (%) | Q (ms) | VO (KB) | V (ms) |
|---|---|---:|---:|---:|---:|
| ANNProof | measured | 97.00 | 93.99 | 58303.94 | 320.31 |
| VLAH | measured | 96.00 | 463.48 | 288373.61 | 563.88 |
| V3DB | measured | 25.50 | 512825.50 | 235.36 | 13.36 |
| MR-tree | measured | 100.00 | 4250.05 | 6148103.95 | 5335.39 |
| Ours | measured | 96.50 | 58.68 | 9318.90 | 87.76 |

Default selection is the smallest tested operating point reaching>=95% Recall, retaining the actual achieved recall. The finite grid can overshoot95%; these defaults are approximately matched, not exactly equal-recall claims. V3DB uses a separately reported lower-recall feasible configuration with one fixed query100 proof; its Recall comes from20 search-only queries and is not the recall of one sample treated as an average. MR-tree performs exact kNN.

## Table4 raw measurements

| Method | Status | R (%) | Q (ms) | VO (KB) | V (ms) |
|---|---|---:|---:|---:|---:|
| IVF-ADS | measured | 96.00 | 7074.45 | 1483631.84 | 3734.29 |
| IVF-ADS+(i) | measured | 96.00 | 879.02 | 245395.19 | 2345.01 |
| NSW-ADS | measured | 97.00 | 255.03 | 57221.92 | 149.44 |
| NSW-ADS+(i) | measured | 97.00 | 170.37 | 38869.48 | 210.19 |
| (i)+(ii) | measured | 96.50 | 66.03 | 10863.31 | 113.83 |
| QVANN | measured | 96.50 | 56.03 | 9318.90 | 81.74 |

The first two pairs preserve their exact search decisions/results while changing progressive disclosure; the final pair preserves SQ4 traversal and results while changing authentication organization. Every normal packet is checked by the existing VO-only verifier and negative tests. Params, native CSV paths and hashes are in table4.csv/json and records/.

IVF-ADS → IVF-ADS+(i): measured Q/VO/V reductions 87.57% / 83.46% / 37.20%. A negative reduction is a regression and is retained.

NSW-ADS → NSW-ADS+(i): measured Q/VO/V reductions 33.20% / 32.07% / -40.65%. A negative reduction is a regression and is retained.

(i)+(ii) → QVANN: measured Q/VO/V reductions 15.14% / 14.22% / 28.19%. A negative reduction is a regression and is retained.

## Recall curves and limits

Ours: 13 distinct actual parameter settings, measured Recall range 15.50%–100.00%. Missing target columns: [].

ANNProof: 11 distinct actual parameter settings, measured Recall range 51.50%–100.00%. Missing target columns: [].

VLAH: 10 distinct actual parameter settings, measured Recall range 21.00%–99.50%. Missing target columns: [1.0].

V3DB: 2 distinct actual parameter settings, measured Recall range 20.50%–25.50%. Missing target columns: [0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 0.97, 0.98, 0.99, 1.0].

Targets in the worksheet are requests, not substituted x coordinates. Every curve recall row contains actual recall; multiple targets can map to the same tested point. The chart sheet deduplicates these points and uses actual Recall on x. ANNProof cannot be claimed to reach40% merely because its first measured point is used in the40% target column. Gaps mean not measured/reached under this declared grid and budget, not a proof of theoretical impossibility. MR-tree supplies only the exact100% endpoint.

V3DB additionally considers a finer PQ family because the initial M128/K256 model has only41% full-probe Recall. Shape-only gate preflights select the largest feasible tested K for M256 before accuracy/timing are observed; a configuration is measured only if its real proof completes. This changes PQ parameters, not just n_probe; model/M/K are explicitly retained in CSVs and chart parameter cells. Original M128 measurements are retained. The final default selects highest actual recall among completed proof configurations. See V3DB-tuning-finished.json for the actual outcome. This is a limited tuning study, not a proof of globally optimal PQ configuration.

## Coefficients and workbook layout

work9.xlsx adds OpenAI-ArXiv-1M, ArXiv原始实测, ArXiv实测与来源 and ArXivRecall曲线. The main comparison/curves retain the prior explicitly derived ANNProof* display factors Q×1.2,VO×2,V×1.5; Recall and indexes are not scaled. Raw native values remain in the raw sheet and all primary CSVs. overall.tex is raw; overall_adjusted.tex is the explicitly labeled coefficient display. Table4 is raw, with no inherited ANNProof or NSW multiplier. No smoothing/monotonic correction/prediction is applied to measured results.

## Offline construction and resource scope

Index size and construction time are independent actual builds and serving snapshots, not query-benchmark cache load times. Native ADS graph builds use up to8 offline threads; MR-tree construction is serial; V3DB original training uses16 coarse Faiss threads and8 single-thread PQ workers. Do not compare these construction times with older single-thread rows without matching resources. The actual host is recorded in actual-environment.json; it is not an unmeasured512GB machine. Proof processes use the existing50GiB address-space cap.

Native hash implementations are retained: ANNProof uses Keccak-256, while the other ADS implementations and Table4 use SHA-256 (MR-tree also signs its root with Ed25519). Their measured verification times include this implementation difference; this experiment does not claim a uniform SHA-256 microbenchmark.

Construction timing ends after the search index/model and authentication structures are built; the subsequent serving.snapshot export is excluded. Native Faiss graph builders write their graph cache within the build timer, whereas MR-tree serializes its serving snapshot separately. Consequently these are native construction scopes, not a uniform end-to-end durable-index I/O benchmark. Snapshot export wall time is not query latency.

ANNProof: actual serving index 6474.79MB, measured construction 751.52s. See its index provenance for exact components and thread settings.

VLAH: actual serving index 6443.34MB, measured construction 248.07s. See its index provenance for exact components and thread settings.

V3DB: actual serving index 1047.75MB, measured construction 308.50s. See its index provenance for exact components and thread settings.

MR-tree: actual serving index 6404.67MB, measured construction 8.35s. See its index provenance for exact components and thread settings.

Ours: actual serving index 7716.71MB, measured construction 764.63s. See its index provenance for exact components and thread settings.

V3DB static gate limits are declared experiment budgets. A budget stop must not be presented as an intrinsic RAM lower bound or as evidence that512GB cannot run that shape. The gate counts, actual attempts and raw failure logs are preserved; missing higher-recall costs are not forecast. A successful gate-only preflight is not a successful query/proof measurement.

## Reproduction and integrity

See README.md for preparation, run/resume and audit commands. Query native implementations and old experiment/workbook data are retained. Independent audits check source bytes, paired ablation results, raw-to-display factors, the complete1536D QVANN wire protocol and all new worksheet cells. The original workbook is backed up before modification.
