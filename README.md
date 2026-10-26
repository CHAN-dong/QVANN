# QVANN: A Quantization-Driven Authenticated Index for Efficient Verifiable ANN Search

QVANN supports verifiable approximate nearest neighbor search over outsourced vector data. It reduces verification-object (VO) size through three coordinated designs:

- **Progressive verification:** SQ4 → SQ6 → raw refinement with safe distance bounds.
- **Verification-decoupled traversal:** Coarse-precision graph search followed by result certification.
- **Expansion-centric authentication:** Expansion records, BFS refinement ordering, and shared Merkle proofs.

## Structure

```text
src/
├── index/       # Graph, quantization, and authenticated index
├── optimize/    # Refinement, traversal, and expansion optimizations
├── query/       # Search, top-k selection, and VO generation
├── verify/      # Authentication, replay, and result verification
├── common/      # Shared types and serialization
├── utils/       # Input, configuration, and measurement
└── main.cpp
tests/           # Quantization, proof, and tamper checks
scripts/         # Run and smoke-test launchers
```

## Build

Requires Linux/WSL, C++17, CMake 3.18+, OpenSSL, OpenMP, and Python 3.

Provide the compatible modified Faiss/LightGBM dependencies externally. `QVANN_DEPS_DIR` must contain `LearnedEarlyTer-20SIGMOD/` with its headers, `libfaiss.so`, and `lib/lib_lightgbm.so`. The fork exposes `hnsw.lselect`; stock Faiss is not a drop-in replacement.

```bash
git clone https://github.com/CHAN-dong/QVANN.git
cd QVANN
export QVANN_DEPS_DIR=/path/to/external/dependencies
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
python3 scripts/smoke.py
```

## Run

Prepare `sift_base.fvecs` and `sift_query.fvecs` in one directory. Each record contains a little-endian int32 dimension followed by float32 coordinates. Dimensions must match and be divisible by four. Normalize NYTimes/GloVe base and query vectors to unit length.

```bash
python3 scripts/run.py \
  --dataset /path/to/vectors --preset sift \
  --output results/sift
```

Presets: `sift`, `nytimes`, `glove`, `deep`. Override settings with `--base`, `--M`, `--ef-construction`, `--budget`, and `--k`. The launcher defaults to 20 queries starting at offset 100, repeated three times; provide at least 120 query vectors.

`--budget` is the unique-node expansion budget, not conventional HNSW `efSearch`. Run `./build/qvann --help` for executable options. Measurements are written locally to `summary.csv` and `per_query.csv`; use a new or empty output directory.

Verification certifies the prescribed ANN protocol over the committed index; it does not guarantee exact global nearest neighbors. The repository includes only QVANN code and supporting scripts; datasets, baseline implementations, papers, result tables, and prebuilt dependencies are excluded.
