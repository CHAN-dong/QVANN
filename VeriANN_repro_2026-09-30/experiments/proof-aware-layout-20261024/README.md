# Reproduce the measured routing-layout experiment

From the inner project directory, with the existing WSL environment/data/graph caches:

```powershell
wsl -e /root/.venvs/v3db-work9/bin/python scripts/run_proof_aware_layout_20261024.py all --run-id proof-aware-layout-20261024-rerun
wsl -e /root/.venvs/v3db-work9/bin/python scripts/audit_proof_aware_layout_20261024.py --run-id proof-aware-layout-20261024-rerun
```

The same run-id resumes completed datasets and checks source/binary/graph provenance. Use a new run-id to obtain fresh timings. C++ build command: build.json. Query IDs, seed and algorithm: protocol.json, sift1m/split.json, deep1m/split.json. Source changes are isolated to src/experiments/proof_aware_layout.cpp and the two new scripts; existing benchmark/protocol sources remain untouched. Audit reuses only pure Python function definitions from scripts/audit_routing_precision_20261024.py and records its hash.

Use proof_aware_layout.tex directly in a PVLDB manuscript with `booktabs` and `graphicx` loaded. It uses a single-column table environment, two decimal places and columnwidth scaling. analysis.tex provides an honest negative-result paragraph. REPORT.md explains metrics, training/test gap, per-query regressions, construction/storage costs and limits. CSV files retain unrounded raw values; samples_all.csv includes all2400 formal authenticated trials. Per-dataset raw files, optimizer iteration history, full permutations, example VOs, roots, negative tests and traces are under results/<run-id>/. All sizes in this experiment use KiB=1024bytes, unlike older work9 tables that used decimal KB. No synthetic coefficient, prediction or workbook update is applied.
