# QVANN

Only the QVANN ANN implementation, its required local source files, and build/run scripts are included. Historical source names also use VeriANN.

The implementation combines SQ4 graph traversal, progressive SQ4/SQ6/raw refinement, query lookup tables, direct-content expansion-centric Merkle authentication, BFS refinement ordering, and VO verification.

`src/qvann_benchmark.cpp` is the executable entry point. The four files in `src/experiments/` form its existing implementation include chain; they are required QVANN code. `paper_repro_common.hpp` supplies shared serialization, hashing and runtime helpers. Its unused Keccak backend is removed in this package; QVANN uses SHA-256.

No papers, external baseline implementations, datasets, indexes, spreadsheets, result tables, measured results, or prebuilt libraries are distributed here.

## Build and run

Linux/WSL requires C++17, OpenMP, OpenSSL headers, Python 3, and the compatible modified Faiss/LightGBM dependencies used by this implementation. Those dependencies are external and are not included in this repository. The Faiss fork exposes `hnsw.lselect`; stock Faiss is not a drop-in replacement.

Point `QVANN_DEPS_DIR` at an external directory containing `LearnedEarlyTer-20SIGMOD/` (headers, `libfaiss.so`, `lib/lib_lightgbm.so`) and, if needed, matching runtime libraries in `runtime/lib/`.

```bash
export QVANN_DEPS_DIR=/absolute/path/to/external/third_party
bash scripts/build.sh
python3 scripts/smoke_test.py
python3 scripts/run_benchmark.py --dataset /path/to/vectors --preset sift
```

The input directory must contain `sift_base.fvecs` and `sift_query.fvecs`. Dimensions must be divisible by four. The smoke check generates temporary synthetic input and checks honest verification and rejection of tampered proofs. Runtime-generated outputs remain local and are ignored by Git.
