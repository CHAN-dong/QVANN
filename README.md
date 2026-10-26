# QVANN: A Quantization-Driven Authenticated Index for Efficient Verifiable ANN Search

This repository provides the implementation of **QVANN**, a quantization-driven authenticated index for **verifiable approximate nearest neighbor (ANN) search** over outsourced high-dimensional vectors.

QVANN jointly designs ANN search and authentication to reduce two major sources of verification-object (VO) overhead: **high-dimensional vector disclosure** and **fragmented Merkle authentication**. It combines coarse-precision graph traversal with progressive result certification, allowing clients to verify query execution using compact evidence.

The framework consists of three main components:

1. **Quantization-Driven Verification:** Uses progressively refined vector representations and safe distance bounds to certify candidates while limiting full-vector disclosure.
2. **Verification-Decoupled Traversal:** Drives graph exploration using coarse-precision representations and reserves exact distances for final result determination.
3. **Expansion-Centric Authentication:** Authenticates graph-expansion records and separately committed refinement data, with graph-aware ordering and shared Merkle proofs.

## Features

- **Coarse-precision graph search:** SQ4 representations guide graph traversal under an explicit expansion budget.
- **Progressive result certification:** Refines ambiguous candidates from SQ4 to SQ6 and, when necessary, to full-precision vectors.
- **Safe candidate exclusion:** Uses quantization-derived lower bounds to certify that excluded candidates cannot improve the final result within the prescribed search protocol.
- **Expansion-centric authentication:** Groups the routing information needed for an expansion into authenticated records.
- **Graph-aware proof sharing:** Uses HNSW-BFS refinement ordering and canonical shared Merkle multiproofs.
- **Query lookup tables:** Accelerates quantized distance and bound evaluation.
- **VO-based client verification:** Checks authentication, deterministic traversal replay, and final result certification against trusted commitments.
- **Executable integrity checks:** Includes honest-query verification and rejection tests for tampered proofs and query inputs.

---

## Overview

QVANN targets an outsourced-search setting with three parties:

- **Data owner:** Constructs the authenticated index and provides trusted commitments and metadata.
- **Cloud server:** Stores the vectors and index, executes the prescribed ANN query, and returns the result with a VO.
- **Query user:** Verifies the returned result using the query, VO, and trusted owner information.

### Quantization-Driven Verification

Coarse representations reduce the amount of vector information needed for verification. Candidates whose lower bounds already rule them out can be excluded without disclosing their full vectors. Ambiguous candidates receive additional precision until they can be certified or evaluated exactly.

The current implementation uses **SQ4 → SQ6 → raw** refinement. SQ6 is represented by the SQ4 code and an additional two-bit refinement per coordinate. Full-precision vectors are disclosed when required for final certification.

### Verification-Decoupled Traversal

Graph traversal and result certification serve different roles. QVANN uses coarse-precision scores to determine graph exploration, then certifies the final candidates through progressive refinement. The client can replay the prescribed traversal without repeatedly reconstructing exact distances for every routing decision.

### Expansion-Centric Authenticated Index

Routing evidence follows graph-expansion accesses. Refinement data are committed separately and disclosed on demand. Graph-aware record ordering increases shared authentication paths, while Merkle multiproofs avoid repeatedly transmitting common proof nodes.

**Verification scope:** Correctness and completeness are defined relative to an honest execution of the prescribed ANN protocol over the committed index. Successful verification does not imply that approximate search has found the exact global nearest neighbors. Recall is evaluated separately against exact ground truth.

---

## Repository Structure

```text
QVANN/
|-- src/
|   |-- qvann_benchmark.cpp          # QVANN entry point, VO generation, and verification
|   |-- veriann_query_lut.hpp       # Query lookup tables for quantized evaluation
|   |-- verifiable_ann_common.hpp  # Vector loading, graph access, and shared primitives
|   |-- paper_repro_common.hpp     # Serialization, hashing, configuration, and measurement
|   `-- experiments/
|       |-- relaxed_hnsw_harvest.cpp
|       |-- e_budget_relaxed_hnsw.cpp
|       |-- expansion_centric_merkle.cpp
|       `-- refinement_merkle_ordering.cpp
|-- scripts/
|   |-- build.sh                   # Builds the QVANN executable
|   |-- run_benchmark.py           # Configures and runs QVANN
|   `-- smoke_test.py              # Temporary synthetic-input integrity checks
|-- .gitignore
`-- README.md
```

The four files under `src/experiments/` form the existing implementation include chain used by QVANN. They are required source components, rather than separately packaged comparison methods. Some source identifiers and output labels retain the historical name **VeriANN**.

This repository contains QVANN source code and supporting scripts. Papers, baseline implementations, datasets, cached indexes, result spreadsheets, measured result archives, and precompiled dependencies are not included.

---

## Requirements

### C++ and System Environment

- Linux or Windows with WSL; the supplied build script targets Linux.
- A C++17 compiler, such as `g++`.
- OpenMP support and OpenSSL development headers/libraries.
- The compatible modified Faiss/HNSW dependency and matching LightGBM library used by the implementation.

**Dependency compatibility:** The code accesses the modified Faiss interface `hnsw.lselect`. A stock Faiss installation is not a drop-in replacement. The required fork and libraries must be supplied externally; they are not downloaded or built by `scripts/build.sh`.

### Python

Python 3 is used for launching benchmarks and running the smoke check. The provided Python scripts use the standard library and do not require model-training packages.

### External Dependency Layout

Set `QVANN_DEPS_DIR` to an absolute Linux/WSL path with the following layout:

```text
/path/to/external/dependencies/
|-- LearnedEarlyTer-20SIGMOD/
|   |-- IndexHNSW.h
|   |-- index_io.h
|   |-- ...                        # Remaining compatible dependency headers
|   |-- libfaiss.so
|   `-- lib/
|       `-- lib_lightgbm.so
`-- runtime/
    `-- lib/                       # Matching runtime libraries, when needed
```

The dependency directory name is retained for compatibility with existing include paths. It does not add a separate comparison method to this repository.

---

## Input Data

Prepare a directory containing:

```text
vectors/
|-- sift_base.fvecs
`-- sift_query.fvecs
```

These filenames are retained by the loader and can also hold appropriately prepared non-SIFT vectors. Each `.fvecs` record contains a **little-endian int32 dimension**, followed by that many **float32 coordinates**.

Before running:

- Ensure that base and query vectors have matching dimensions and consistent preprocessing.
- Use a dimension divisible by **four**, as required by the current packed quantization implementation.
- Provide at least `--base` base vectors and `--query-offset + --queries` query vectors.
- Use unit-normalized base and query vectors for the paper's NYTimes and GloVe settings. The executable uses squared L2 distance, which preserves cosine-similarity rankings for unit vectors.

Exact ground truth for the selected base subset and queries is computed by the executable. A separate ground-truth file is not required.

---

## Quick Start

### 1. Clone the repository

```bash
git clone https://github.com/CHAN-dong/QVANN.git
cd QVANN
```

### 2. Configure external dependencies

```bash
export QVANN_DEPS_DIR=/absolute/path/to/external/dependencies
```

Keep this variable set for both building and running. Windows users should execute these commands inside WSL and use Linux paths such as `/mnt/d/...`.

### 3. Build QVANN

```bash
bash scripts/build.sh
```

The executable is generated at `build/qvann`. The compiler can be selected through the `CXX` environment variable.

### 4. Run the smoke check

```bash
python3 scripts/smoke_test.py
```

The check creates **2,048 synthetic 16-dimensional base vectors** and **120 query vectors** in a temporary directory. It verifies **20 queries over three repetitions**, checks successful verification of all 60 executions, and checks rejection of tampered evidence. Temporary inputs and outputs are removed after the check.

### 5. Evaluate your dataset

For the SIFT preset:

```bash
python3 scripts/run_benchmark.py \
  --dataset /absolute/path/to/vectors \
  --preset sift \
  --output results/sift-default
```

For a custom configuration:

```bash
python3 scripts/run_benchmark.py \
  --dataset /absolute/path/to/vectors \
  --base 100000 \
  --M 16 \
  --ef-construction 100 \
  --budget 64 \
  --k 10 \
  --queries 20 \
  --query-offset 100 \
  --repeats 3 \
  --cache cache/custom \
  --output results/custom-run
```

Use a new or empty output directory for each run. Index caches are generated locally and reused when applicable.

---

## Configuration

The launcher exposes the following main parameters:

- `--dataset`: Directory containing the base and query `.fvecs` files.
- `--preset`: One of `sift`, `nytimes`, `glove`, or `deep`.
- `--base`: Number of base vectors to load.
- `--M`: HNSW graph-construction connectivity parameter.
- `--ef-construction`: HNSW construction search parameter.
- `--budget`: QVANN expansion budget **E**.
- `--k`: Requested result size; default **10**.
- `--queries`: Number of evaluated queries; default **20**.
- `--query-offset`: Starting query index; default **100**.
- `--repeats`: Repetitions per query; default **3**.
- `--cache`: Local directory for generated index caches.
- `--output`: Local output directory.

**Expansion-budget semantics:** The launcher passes `--budget` to the executable as `--ef-search`, and generated CSV files retain the field name `ef_search`. For QVANN, this value denotes **E**, the expansion budget, rather than conventional HNSW `efSearch`.

The current launcher presets are:

- **SIFT:** 1,000,000 vectors; M = 8; construction parameter = 100; E = 128.
- **NYTimes:** 289,761 vectors; M = 16; construction parameter = 200; E = 256.
- **GloVe:** 1,183,514 vectors; M = 16; construction parameter = 200; E = 1,536.
- **DEEP:** 1,000,000 vectors; M = 16; construction parameter = 200; E = 64.

Explicit arguments override the corresponding preset values. Without a preset, the launcher defaults to 100,000 base vectors, M = 16, construction parameter = 100, and E = 64. Presets are configuration starting points, not recall guarantees for newly prepared data or rebuilt graphs.

---

## Runtime Outputs

Each evaluation generates local files, including:

- `summary.csv`: Aggregated recall, VO size, timing, and index/authentication statistics.
- `per_query.csv`: Per-execution measurements and verification status.
- `tamper_tests.csv`: Rejection outcomes for integrity checks.
- `query0.vo`: A serialized example verification object.
- `trusted_roots.txt`: Authentication roots used by the local evaluation.
- `invocation.json`: Launcher arguments and execution configuration.
- `manifest.json` and `optimizations.json`: Run metadata and implementation settings.

The launcher fixes relevant thread settings to one for controlled evaluation. Search, VO generation, authentication, and verification times are reported separately. Timing excludes network transport. VO fields expressed in **KiB** use 1,024 bytes; compare units explicitly when relating these outputs to paper figures.

The benchmark evaluates owner/server/client responsibilities within one local executable. Trusted roots must be delivered through an authenticated owner channel in a deployment; writing them to a local file is not an implementation of that channel. The client verification routine uses trusted metadata and the VO without requiring the server's complete graph or vector arrays.

Generated measurements, data, indexes, and binaries remain local and are excluded by `.gitignore`.

---

## Experimental Datasets

The associated paper evaluates four real-world vector datasets:

- **SIFT:** 128-dimensional local image descriptors from BIGANN/SIFT1B; SIFT1M is the default setting.
- **NYTimes:** 256-dimensional document vectors; removing 239 zero vectors from the original 290,000 leaves 289,761 objects.
- **GloVe:** 1,183,514 word embeddings with 100 dimensions.
- **DEEP:** 96-dimensional deep visual descriptors from DEEP1B; DEEP1M is the default setting.

The paper additionally evaluates SIFT and DEEP prefixes of up to 10 million vectors for scalability. Dataset descriptions summarize the paper's evaluation scope; this source-only repository does not redistribute these datasets or their experimental result tables.

---

## Notes

- This is research code for QVANN query execution and verification.
- The current executable supports squared L2 distance and Merkle multiproofs with SHA-256.
- Rebuilding the graph may produce different recall and timing from a historical cached index. Keep data preprocessing, index construction, query selection, and thread settings consistent when comparing runs.
- Large datasets incur additional exact-ground-truth and index-construction costs before query evaluation.
- The smoke check validates basic execution and integrity behavior; it does not reproduce the paper's full experimental evaluation or replace its security analysis.
- The paper's publication metadata and any eventual code license should be taken from the authors' finalized release; this README does not assign a new license.

## Contact

For questions about the research or implementation, contact **Dong Wang** at **dong.wang@chd.edu.cn**.
