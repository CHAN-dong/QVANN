# Recall curves: common targets 60% to 99%

SIFT1M; k=10; M=8; efConstruction=100; identical query IDs 100..199; 3 repeats; single-thread query and verification.
Common target thresholds; main table and charts use actual measured recall. Each parameter is the smallest tested parameter reaching its threshold, not a global optimum.
Pilot and evaluation use the SAME 100 queries to match the workload recall. This is not an independent held-out recall estimate. No interpolation or extrapolation.
VLAH uses efSearch=100 throughout this new curve (previous curve: 50), varying probes; ANNProof varies efSearch; Ours varies E using validated LUT implementation.
Query time includes actual VO generation/serialization; verification includes client decoding/authentication/replay; KB=1000 bytes; no network/blockchain time.
ANNProof and VLAH are disclosed algorithmic reimplementations, not strict author-code/full blockchain reproductions.
Historical configuration/resource rows were retained and marked superseded; current curve provenance is this sheet and main-cell comments.
60% is the common target start; zero-recall Ours points were removed from these comparison curves. Discrete parameters cause small differences in actual starting recalls.

| Method | Actual start | Actual end | Query ms at >=99% | Verify ms | VO KB | Parameter |
|---|---:|---:|---:|---:|---:|---|
| VLAH | 60.7% | 99.1% | 71.595 | 176.971 | 26276.076 | ef/E=100, probes=47 |
| ANNProof | 62.1% | 99.0% | 11.153 | 47.281 | 3757.365 | ef/E=512, probes=1 |
| Ours | 62.3% | 99.0% | 6.881 | 12.291 | 599.296 | ef/E=512, probes=1 |

198 curve cells verified; 9900 formal measurements; original workbook updated: True.
Backup: /mnt/d/paper_source/work_9/VeriANN_repro_2026-09-30/VeriANN_repro_2026-09-30/experiments/work9-recall99-20261015/work9_before_recall99.xlsx
