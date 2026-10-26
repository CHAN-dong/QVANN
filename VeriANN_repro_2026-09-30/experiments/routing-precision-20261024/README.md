# Impact of Routing Precision ? measured ablation

The final table is `routing_precision.tex`; `analysis.tex` is a short paper-ready discussion. Summary with actual parameters is `summary.csv`, and all480 measured query/proof/verification records are `samples_all.csv`. Full native data, trusted anchor metadata, exact per-expansion scores, retained complete VOs and authenticated-but-wrong-routing witnesses are in `results/routing-precision-20261024/{sift1m,deep1m}/formal/`.

Both routers use the identical cached graph for each dataset, k10, query IDs100..119,20 queries?3 repeats, one thread, original SQ4 route/delta-SQ6/raw Merkle trees and canonical BFS-ordered multiproofs. Exact authenticates raw vectors for every scored candidate, not just returned results; routing/final raw records are deduplicated and computed exact distances are cached and reused. The verifier has no server graph/database. A shared4-byte mode tag is charged to both modes. Removing it makes every SQ4 VO byte-identical to the original implementation.

Fixed E: SIFT128, DEEP64. The20-query subset realizes recalls above95% at these inherited defaults. Matched Recall: dense independent E1..256 calibration on the same20 queries gives SIFT Exact88/SQ4111 and DEEP Exact56/SQ449, all four at95.00%. These are in-sample calibrated operating points, not a held-out accuracy claim.

Run from the inner project root using the existing WSL Python environment:

```
wsl -e /root/.venvs/v3db-work9/bin/python scripts/run_routing_precision_20261024.py all
wsl -e /root/.venvs/v3db-work9/bin/python scripts/audit_routing_precision_20261024.py
```

The default run name resumes existing validated checkpoints. To actually remeasure into isolated new directories, choose a new run name:

```
wsl -e /root/.venvs/v3db-work9/bin/python scripts/run_routing_precision_20261024.py all --run-id routing-precision-fresh
wsl -e /root/.venvs/v3db-work9/bin/python scripts/audit_routing_precision_20261024.py --run-id routing-precision-fresh
```

Stages `build`, `pilot`, `formal`, and `report` can be run separately. Actual compiler invocation is preserved in `build.json`; source/binary and graph hashes, environment/commands and selection policy are preserved alongside the raw files. Each process reads existing million-vector data and historical graph caches; missing prerequisites produce errors rather than estimates. The original QVANN code, historical graph caches and workbook are untouched.

New files: `src/experiments/routing_precision.cpp`, `scripts/run_routing_precision_20261024.py`, `scripts/audit_routing_precision_20261024.py`. The generated `routing_precision_original_include.cpp` includes the unchanged original protocol definitions with only their unused CLI main omitted.

Independent audit:20480 calibration rows recounted,480 formal rows checked,44406 expansion scores independently recomputed from original float32 vectors,8 complete VOs independently decoded/Merkle-verified/replayed with Python,4 cryptographically authentic SQ4 transcripts independently rejected under Exact semantics. Native checks also confirm240 original SQ4 wire/verifier equivalences and116 negative cases. All old graph hashes remain unchanged. Two-decimal LaTeX presentation marks best/second-best VO and V separately within each dataset/setting pair, and scales to a single PVLDB column with `booktabs`/`graphicx`.
