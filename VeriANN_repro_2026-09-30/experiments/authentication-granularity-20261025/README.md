# Impact of Authentication Granularity

This isolated experiment changes routing authentication only. Existing results and `work9.xlsx` are not written.

## Reproduce

Run from the inner `VeriANN_repro_2026-09-30` project directory using its existing WSL environment:

```powershell
wsl -e /root/.venvs/v3db-work9/bin/python scripts/run_authentication_granularity_20261025.py all
wsl -e /root/.venvs/v3db-work9/bin/python scripts/audit_authentication_granularity_20261025.py
```

Stages `build`, `preflight`, `run`, and `report` are also available. `run` resumes completed native groups without repeating them. After a source change use a new experiment directory; do not overwrite old binary/result identities. Failed groups retain logs and a `FAILED.json`. `report` exports completed actual data and marks failures/pending entries explicitly, leaving their metrics empty.

The final workbook is copied to `../authentication_granularity.xlsx`; a second copy remains in this directory. `Raw_Data` contains 24 configuration rows. `SIFT_Plot` and `DEEP_Plot` have one row per M and adjacent Record-wise/Node-wise/ER columns for every metric. The first three metric groups are routing proof size, total VO and verification time; three ordinary line charts are included. `Config` contains exact invocations, graph/data identities, calibration selections, implementation definitions and environment. Metric cells are numeric OOXML cells, not formatted strings or formula caches.

## Controls

- SIFT1M (128D) and DEEP1M (96D), unmodified float32 vectors, squared L2, k=10.
- HNSW M=8,16,32,64; common efConstruction=200 across all M. M is the HNSW parameter, whose level-0 capacity is 2M, not a guarantee that every node has exactly M neighbors.
- Reuse matching existing graphs read-only; build missing graphs in the new results directory. Offline construction uses eight threads; all query/proof/verification measurements use one thread.
- Original query IDs 100–119, three repeats, one untimed warmup per query/variant. Rotate variant execution order by query and repeat. Query timing includes SQ4 LUT preparation, traversal, exact candidate ranking, actual VO generation and multiproofs. Verification includes wire decoding, all authentication, SQ4 replay and final result certification. No transport time.
- Calibrate one E per dataset/M using search only, then use that E for all three variants. Coarse grid stops at the first sampled Recall>=95%; densely test the immediately preceding interval. Select closest actual Recall to95%, tie prefer >=95%, then lower E. This is a documented finite calibration policy, not a claim of a globally optimal E. Record actual Recall; do not replace it by95%.
- Calibration and measurement share the same20 independent query IDs. Repetition does not create60 independent recall samples; this is not a held-out quality claim.
- Identical SQ4 scores, traversal, candidate IDs, exact top10, SQ4--SQ6--raw disclosure and native BFS refinement Merkle trees. Routing leaves are ID-major, not proof-aware/BFS optimized.

## Authentication definitions

**Record-wise** means a separate authenticated record for every directed edge `(source, layer, slot, neighbor)`, node metadata `(ID, edge base, layer degrees)`, and discovered node SQ4 code. The degree/offset metadata binds completeness: a server cannot omit part of an opened adjacency list. SQ4 codes are whole packed vectors, not per-coordinate leaves. Three routing trees use canonical multiproofs over all targets in each tree.

**Node-wise** means a complete multi-layer adjacency list per expanded node, with discovered SQ4 codes authenticated separately. Unopened layers are disclosed and authenticated as part of that node record but are not scored or used to alter traversal. It uses two routing-tree multiproofs. This is distinct from an adjacency-record-per-node/layer design.

**ER** is the original per-node/layer expansion record, jointly committing neighbor IDs and their SQ4 codes. Neighbor codes are deduplicated in the wire. It uses one routing multiproof. Removing the common4-byte variant tag recovers a byte-identical original QVANN VO, which is also checked by the original native verifier.

This comparison uses these explicit leaf/root organizations. It does not establish optimality over every possible record definition or a combined-root leaf layout. There are no per-target independent Merkle paths, forced code duplication, inherited display factors, smoothing, or simulated results.

## Accounting and validation

KB=1000 bytes. Routing proof size includes4 bytes per multiproof's sibling-count field and32 bytes per sibling digest. Routing payload includes SQ4 codes, adjacency/node/edge records and their required metadata. It excludes the shared query/result header, common variant tag and refinement; all these remain in Total VO. Client trust anchors are setup metadata, as in the existing implementation.

Every normal packet must pass the VO-only native client verifier without access to the server graph/data. Server/client fingerprints, candidates, recall and refinement byte fingerprints must match across variants. Per-group negative tests corrupt mode/header/query/answer/code/adjacency/refinement/raw/metadata/proof/root/budget and test truncation/appending. ER is checked byte-for-byte against the original serializer and original verifier. The separate Python audit independently recomputes all three variants' leaf hashes, canonical multiproofs, float32 SQ4 replay and progressive top-k certificates for all24 saved sample packets.

New source: `src/experiments/authentication_granularity.cpp`; driver/exporter: `scripts/run_authentication_granularity_20261025.py`; independent verifier: `scripts/audit_authentication_granularity_20261025.py`. The driver extracts the existing native serializer/verifier and changes only routing record/proof handling in generated includes. It reuses existing `optimized::walk`, Codec/LUT, ADS refinement trees, `paper_repro::Tree`, canonical multiproof routines and final verification checks. The earlier `expansion_centric_merkle.cpp` is a size-oriented probe with different record definitions; it is not substituted for an end-to-end client verification experiment.

Raw native samples, anchors, complete example VOs, test outcomes, exact graph identities and resource logs are in `results/authentication-granularity-20261025`. Aggregated CSV/JSON, the workbook and audits are in this directory. Runtime results should be analyzed from completed measurements, including any regressions; no monotonicity is imposed as M changes and E is recalibrated.
