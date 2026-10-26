# Controlled quantization refinement experiment ? 2026-10-23

Fix the existing SQ4 top-1 graph traversal, graph cache, node budget E, query IDs and candidate ranking. Change only the refinement chain and its authenticated incremental payloads:

- SQ4--raw
- SQ4--SQ6--raw (existing default)
- SQ4--SQ8--raw
- SQ4--SQ6--SQ8--raw

All four datasets use their existing Ours default graph/scale/E. SIFT uses M8/efc100/E128 at 1M, NYTimes M16/efc200/E256 at 289761, GloVe M16/efc200/E1536 at 1183514, and DEEP M16/efc200/E64 at 1M.

Each point is measured on20 query IDs100..119 repeated3 times, one CPU thread. Rotate configuration order across queries. Recall is a control, not a new tuning target; SIFT's20-query mean can differ from its existing100-query overall mean. Every measured query generates a real serialized VO and verifies it on a client that has only quantization metadata and trusted roots. Times include query-specific LUT preparation. KB means1000 bytes. No performance multipliers or estimates are used.

SQ8 extends the original uniform SQ6 codes exactly. Incremental payloads contain2bits/dim for4?6,4bits/dim for4?8, and2bits/dim for6?8. Its reconstructed intervals include a conservative float32 rounding envelope before outward rounding, validated against every coordinate. A one-ULP-only trial was rejected on NYTimes; those diagnostic runs are archived in initial-interval-check and are not included in final results.

For each query/repeat, all configurations must have identical traversal fingerprints and ordered answers. The SQ4--SQ6--raw wire must exactly match the original protocol's wire, and the original verifier must accept it. Tamper cases cover payloads/proofs, truncation/appended data, wrong roots, wrong query and wrong traversal budget. All original workbook values, comments and cell formats are preserved.

Reproduce from the project root using the WSL v3db-work9 Python environment:

```
python scripts/run_work9_quantization_20261023.py
python scripts/fill_work9_quantization_20261023.py
```

The fill script applies only once and checks that work9.xlsx still matches its original backup. Raw data are in results/work9-quantization-20261023. Protocol, compile command/hashes, summary, audit, workbook backup, and two ready-to-use LaTeX tables are stored here.
