# VeriANN reproducibility package

This directory is a self-contained snapshot of the VeriANN research code as of 2026-09-30. The primary reproducible path is the SIFT100K, `k=10`, 100-query line of work ending in SQ4 top-1 traversal, Direct-content Expansion-Centric Merkle authentication, and refinement-tree ordering.

## Quick start

The independent author-code V3DB reproduction is available in
[V3DB-26VLDB](V3DB-26VLDB/README.md), including a pinned upstream checkout,
Windows/WSL launchers, actual proof generation and verification for both designs,
a real SIFT subset run, and a negative commitment-consistency test. See its
[measured results and limitations](V3DB-26VLDB/RESULTS.md) before comparing costs.

The three additional dataset workbook sheets (GIST1M, GloVe-100, DEEP1M) are
documented in [work9 multidata study](experiments/work9-multidata-20261015/README.md).
Run `scripts/run_work9_multidata_windows.ps1` to resume measurements and audit
the completed workbook. Pending cells are replaced only by verified measurements.

The latest three-method recall curves use common target thresholds from 60% to
99%: [work9 recall99 study](experiments/work9-recall99-20261015/README.md).
This replaces the earlier recall curves while preserving the other experiments.

The latest full-workbook timing refresh uses the query lookup-table version:
[work9 LUT measurements](experiments/work9-ours-lut-20261015/README.md).
Run `scripts/run_work9_lut_windows.ps1` to resume/check that study. It remeasures
every displayed Ours configuration at its original work point and preserves
the earlier scalar measurements and workbook backup.

The fully combined VeriANN wire-protocol benchmark and filled-workbook study are
documented in [work9 Ours experiments](experiments/work9-ours-20261015/README.md).
It combines SQ4 top-1 traversal, progressive refinement, direct-content expansion
records, BFS refinement ordering, and sparse canonical multiproofs. It measures
actual serialized VO bytes and client authentication/replay rather than the older
payload accounting alone. Run `scripts/run_work9_ours_windows.ps1` to resume the
study and check its evidence; see the study README before starting a fresh run.

Independent paper reproductions are available in [ANNProof-24FGCS](ANNProof-24FGCS/README.md)
and [VLAH-25TrustCom](VLAH-25TrustCom/README.md). They include Windows/WSL launchers,
real serialized verification objects, client authentication and search replay, parameter sweeps,
recall and timing measurements, and tamper self-checks. See [measurement definitions](docs/PAPER_REPRO_METRICS_CN.md)
for scope and implementation differences from the papers.

Windows users: see [Windows / WSL instructions](docs/WINDOWS_RUN_CN.md).
Run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_key_experiments_windows.ps1`
after WSL/Ubuntu and the Linux build dependencies are ready. Each Windows run saves fresh logs
and CSV files under a separate `results/windows-*` directory.

From this directory:

```bash
bash scripts/build_key_experiments.sh
bash scripts/run_key_experiments.sh
python3 scripts/compare_key_results.py
```

The build script creates three binaries in `build/`. The run script fixes `OMP_NUM_THREADS=1` and `OPENBLAS_NUM_THREADS=1`, executes the packaged SIFT100K experiments, and writes regenerated CSV files to `results/reproduced/`. The comparison script checks them against `results/key/`.

## Expected headline results

| Experiment | Expected result |
|---|---|
| SQ4 top-1, E=64 | Recall@10 0.992, expanded 64.00, disclosed 811.86, total MAC VO 76.011 KiB |
| Direct-content Expansion-Centric Merkle | Merkle proof 56.392 KiB, total VO 132.341 KiB |
| HNSW-BFS refinement ordering | refinement proof 27.663 KiB, total VO 121.255 KiB |

## Directory layout

- `src/verifiable_ann_common.hpp`: shared data loading, HNSW, Merkle multiproof, VO, and verification primitives.
- `src/experiments/`: all 48 C++ experiment sources from the original `VeriANN` directory.
- `results/key/`: compact set of headline CSV results.
- `results/archive/`: all CSV outputs preserved from the working directory.
- `docs/CODE_WALKTHROUGH_CN.md`: Chinese implementation guide by source line/range; it explains code without reproducing large code blocks.
- `docs/notes/`, `docs/documents/`, `docs/figures/`: research notes, generated documents, and figures.
- `dataset/sift/`: exact first 100,000 SIFT base vectors and first 100 queries used by the key experiments; the included ground truth is for this subset.
- `merkle_hnsw/`: exact cached HNSW index used in the reported runs.
- `third_party/LearnedEarlyTer-20SIGMOD/`: modified Faiss/HNSW source plus the original prebuilt Faiss and LightGBM libraries.
- `third_party/runtime/lib/`: matching OpenBLAS, Fortran, libstdc++, and libgcc runtimes for compatible Linux x86-64 machines.
- `archive/`: original project Makefile/CMake file and patch backups retained for provenance.
- `checksums/SHA256SUMS`: integrity manifest for sources, data, index, libraries, scripts, docs, and key results.

## Platform requirements

The tested environment is Linux x86-64 with GCC supporting C++17 and OpenSSL 3. The bundled binary libraries are convenient for a compatible Linux host. A typical Debian/Ubuntu source-build environment needs:

```text
g++ make python3 libssl-dev libopenblas-dev liblapack-dev libgomp1 gfortran
```

If the bundled shared libraries are ABI-incompatible with the destination machine, rebuild `third_party/LearnedEarlyTer-20SIGMOD` there and replace `libfaiss.so` and `lib/lib_lightgbm.so`, or point the build script at compatible libraries using `VANN_EXTRA_LIB_DIR`.

## Reproduction contract

- Dataset: SIFT100K, dimension 128.
- Queries: first 100 SIFT queries.
- Search: SQ4 top-1 traversal, fixed expansion budget `E=64`, `k=10`.
- Index: packaged `sift100k_M16_efc100.index`; reusing it avoids graph-construction randomness.
- Hash: SHA-256, 32 bytes.
- Merkle proofs: real trees and shared-path multiproofs, with tamper self-checks.
- Refinement: unchanged `SQ4 -> Delta4->6 -> raw` candidate set.
- Ordering ablation: Node-ID, deterministic level-0 HNSW BFS, and lexicographic SQ4 code; no workload statistics.

Small timing differences are normal. CSV count/VO/recall fields should match the references; timing columns are ignored by the comparison script.

## Research-code map

The shortest path through the final design is:

1. `relaxed_hnsw_harvest.cpp` — initial relaxed traversal.
2. `e_budget_relaxed_hnsw.cpp` — single expansion-budget protocol and final-threshold verification.
3. `aggregated_mac_hnsw.cpp` — aggregate-MAC comparison.
4. `progressive_relaxed_hnsw.cpp` — SQ4 to Delta4->6 progressive payload.
5. `sq4_progressive_adjacency.cpp` — primary/residual adjacency experiment.
6. `expansion_centric_merkle.cpp` — record-wise, hashed-neighbor ER, and direct-content ER Merkle comparison.
7. `refinement_merkle_ordering.cpp` — Node-ID/BFS/SQ4 leaf-ordering ablation.

Earlier IVF, block graph, portal, residual, authentication-tree, and VO-allocation experiments remain under `src/experiments/` and `results/archive/` so the full research trajectory is not lost.

Current `work9` dataset replacement: [NYTimes-256 study](experiments/work9-nytimes-20261017/README.md).
GIST1M measurements are archived and excluded from the new active workbook; all methods are rerun on the same nonzero-base NYTimes subset (289,761 vectors, 256 dimensions).
The study records preprocessing, the original-ID map, 20-query × 3-repeat single-thread measurements, and unsupported size points. It does not presume Ours will outperform other methods.

