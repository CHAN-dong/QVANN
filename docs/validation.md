# Build and test results

## Environments

- WSL Ubuntu 26.04.1: GCC 15.2.0, CMake 4.2.3, OpenSSL 3.5.5, OpenBLAS 0.3.32,
  Python 3.14.4.
- [Ubuntu 24.04 CI build](https://github.com/CHAN-dong/QVANN/actions/runs/40656114407):
  GCC 13.3.0, CMake 3.31.6, OpenSSL 3.0.13, Python 3.12.3.
- Plotting and conversion: NumPy 2.5.4, h5py 3.16.0, Matplotlib 3.11.2.

## Checks

- Faiss and LightGBM source build, project compilation, CTest and query smoke checks.
- Full SIFT1M, NYTimes, GloVe and DEEP1M conversion: base payload and query-file
  checksums match the configured inputs.
- QVANN and Figure 16 run on a 5,000-vector real SIFT prefix, queries 100–101.
  Figure 16 covers all four M settings and three authentication variants.
- QVANN, ANNProof, VLAH and MR-tree run on 1,000-vector prefixes of SIFT, NYTimes,
  GloVe and DEEP, using query rows 100–101. All 32 measured query verifications pass.
  The combined CSV/table pipeline also passes on a 5,000-vector SIFT prefix.
- Smoke checks cover multiple ANNProof shards and VLAH buckets, plus MR-tree
  query verification.
- Each standalone baseline completes a 20-query, three-repeat workload
  on the 5,000-vector SIFT prefix, with 180 verified executions across the methods.
- OpenAI-ArXiv: conversion preserves the source float32 bytes; QVANN and all three
  baselines pass query verification on a 1,000-vector, 1536-dimensional prefix.
- Duplicate CSV samples and changed source hashes are rejected by the plotting tool.
- BIGANN uint8-to-float32 conversion preserves coordinates. After the Table 4
  command update and ANNProof SHA-256 change, all three CTest checks and 40 real
  query verifications pass across all five dataset presets and four methods,
  using 1,000-vector prefixes. Table 4 CSV summary generation also passes.

Generated measurements and build logs remain in local run directories. The
`validation/raw/` example uses SIFT's first 5,000 vectors and query rows 100–101.
