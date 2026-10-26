# Experiment datasets

Existing data:

- `sift/`: original packaged SIFT100K reproduction data.
- `sift1m/`: ANN-Benchmarks SIFT1M.
- `bigann10m/`: official BIGANN/SIFT1B first 10M vectors.

Additional agreed datasets:

| Directory | Base vectors | Dimension | Prepared distance |
|---|---:|---:|---|
| `gist1m/` | 1,000,000 | 960 | squared L2 |
| `glove100/` | 1,183,514 | 100 | cosine, represented by squared L2 on normalized vectors |
| `deep1m/` | 1,000,000 | 96 | official DEEP L2 |
| `deep10m/` | 10,000,000 | 96 | official DEEP L2; exact extension of the same prefix |

Preparation is complete only when `extra_datasets_final_validation.json` reports
`PASS`. Check each dataset's `provenance.json` for source URLs, shapes,
preprocessing, SHA-256 fingerprints, and validation details. Original downloads
are retained under each dataset's `raw/` directory.

`deep10m/validation.json` separately validates the DEEP10M extension. Its first
1M converted vectors match `deep1m/base.fvecs` byte for byte and its query file
is a hard link to the same official source. Prepare/resume this extension with
`python3 scripts/prepare_deep10m.py` under WSL; its 5M/10M experiments are in
`experiments/work9-deep10m-20261015/`.

Canonical converted files are `base.fvecs`, `query.fvecs`, and, where provided,
`groundtruth.ivecs`. Legacy `sift_base.fvecs` and `sift_query.fvecs` names are hard
links used by existing C++ loaders. Their contents belong to their enclosing
dataset; the names do not imply that these datasets contain SIFT vectors.

Use `--metric l2` for these prepared files. GloVe's raw HDF5 is unmodified;
converted base and query vectors are both unit-normalized. Do not apply this
normalization to the other datasets without defining a different experiment.

GIST and GloVe include the supplied top-100 neighbors for their full base sets.
DEEP1M is an exact byte-range prefix of official Yandex DEEP1B, not the separate
ANN-Benchmarks DEEP subset. Its ground truth must be computed for the selected
base size. Existing benchmark runners already compute exact neighbors for each
requested query/base workload. Never reuse 1B or 10M neighbors for a 1M base, or
full-dataset neighbors for a smaller subset.

For future scale experiments, extend the DEEP prefix to 5M/10M/100M as needed,
retain the same query source, and compute corresponding exact ground truth.
The 96-dimensional float32 source uses 384 bytes per vector plus an 8-byte
file header. Raw 1M, 10M and 100M prefixes are about 384 MB, 3.84 GB and 38.4 GB;
these are raw storage sizes, not total benchmark memory requirements.

Resume downloading and preparation from Windows PowerShell:

```powershell
./scripts/prepare_extra_datasets_windows.ps1
```

The downloader uses the existing local proxy at `127.0.0.1:7897` when invoking
Windows curl from WSL. It validates each HTTP byte range and consistent ETags,
and reuses completed valid chunks. Preparation and integrity verification run
under the existing Ubuntu WSL environment with NumPy and h5py.

Official benchmark references:

- https://github.com/erikbern/ann-benchmarks#data-sets
- https://big-ann-benchmarks.com/neurips21.html

No new performance experiments are run as part of dataset preparation.
