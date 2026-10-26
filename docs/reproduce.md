# Reproduction notes

## Scope and provenance

The configuration targets manuscript revision (8). `configs/paper.json` records
the setup recovered from Sections 7.1–7.2 and the original local experiment
protocols dated 2026-10-15 through 2026-10-25. It contains configuration and input
fingerprints, not paper result values. Existing workbooks and result files are
never read by the new measurement runner or changed by it.

`table3` measures the **QVANN row** on SIFT, NYTimes, GloVe, and DEEP. `figure16`
measures the three QVANN authentication-granularity variants on SIFT. The
granularity executable reuses the original calibration, serialization, verifier,
and tamper checks, adapted to the current module interfaces. It does not add
external baseline implementations.

## Dependency build

On Debian/Ubuntu, run `bash scripts/install.sh`, then `python3 scripts/deps.py`.
The installer needs sudo for OS packages; the dependency build needs no sudo.
Set HTTPS_PROXY/HTTP_PROXY yourself only if required by your network.

The supplied dependency build targets x86-64 CPUs with SSE4/POPCNT; it does not
provide an ARM or GPU build.

- Faiss: [efficient/faiss-learned-termination](https://github.com/efficient/faiss-learned-termination),
  based on 1.5.2, pinned to `ec2e252e1a316f5a3cb2e108082507ccbfa29ed1`.
- Apply `deps/faiss.patch` to reproduce the locally modified dependency interfaces,
  including prior-aware HNSW fields. These are initialized as in the existing
  QVANN implementation. Patched-file SHA-256 values are pinned in `deps/versions.json`.
- LightGBM: [v2.3.1](https://github.com/microsoft/LightGBM/tree/v2.3.1),
  commit `471d0aaaaa8574b9a1da2302e992f61b01e3d155`.
- C++17, CMake >=3.18, OpenSSL >=1.1.1, OpenMP, and system OpenBLAS.

Both shared libraries are rebuilt from source, including on reruns. The script
replaces the upstream machine-specific Makefile flags with explicit CPU/BLAS
flags. Forced inclusion of `<cstdint>` is needed by the old sources with current
compilers. CMake's policy compatibility flag allows the old LightGBM project to
configure with CMake 4. No prebuilt upstream libraries are used for validation.
Source licenses remain in the fetched repositories.

The output defaults to `.deps/`, ignored by Git. `--prefix /path/to/deps` selects
another location; pass that location as `-DQVANN_DEPS_DIR=/path/to/deps` when
configuring QVANN. A completed dependency build writes source versions, compiler
details, patch checksum and library checksums to `<prefix>/build.json`.

## Data preparation

Download the real source files separately; the repository redistributes no data.

| Dataset | Source | Default size / dimension | Preparation |
|---|---|---|---|
| SIFT | [sift-128-euclidean.hdf5](https://ann-benchmarks.com/sift-128-euclidean.hdf5) | 1,000,000 / 128 | Preserve float32 coordinates |
| NYTimes | [nytimes-256-angular.hdf5](https://ann-benchmarks.com/nytimes-256-angular.hdf5) | 289,761 / 256 | Remove zero base vectors if present; normalize base and queries |
| GloVe | [glove-100-angular.hdf5](https://ann-benchmarks.com/glove-100-angular.hdf5) | 1,183,514 / 100 | Normalize base and queries |
| DEEP | [base.1B.fbin](https://storage.yandexcloud.net/yandex-research/ann-datasets/DEEP/base.1B.fbin), [query.public.10K.fbin](https://storage.yandexcloud.net/yandex-research/ann-datasets/DEEP/query.public.10K.fbin) | 1,000,000 / 96 | First 1M base vectors; preserve float32 coordinates |

Use `curl -fL --retry 3 URL -o FILE` for the HDF5 sources. DEEP's complete base
file is approximately 384 GB. To fetch only the required first-million prefix:

```bash
curl -fL --retry 3 --range 0-384000007 --max-filesize 384000008 \
  'https://storage.yandexcloud.net/yandex-research/ann-datasets/DEEP/base.1B.fbin' \
  -o base.1M.prefix.fbin
```

Require HTTP 206 and `Content-Range: bytes 0-384000007/...`; do not treat a server
that ignores ranges as a successful prefix download. The converter checks that
the requested prefix is present even though the original header declares 1B rows.

```bash
python scripts/prepare.py --dataset sift --source /downloads/sift-128-euclidean.hdf5 --output data/sift
python scripts/prepare.py --dataset nytimes --source /downloads/nytimes-256-angular.hdf5 --output data/nytimes
python scripts/prepare.py --dataset glove --source /downloads/glove-100-angular.hdf5 --output data/glove
python scripts/prepare.py --dataset deep --source /downloads/base.1M.prefix.fbin \
  --queries /downloads/query.public.10K.fbin --output data/deep
```

Normalization uses float64 norms and float32 reciprocal scaling, matching the
original preparation script. NYTimes preserves the nine zero query rows at IDs
795, 1565, 3545, 4066, 4985, 5702, 6620, 6644 and 6864 to retain original query
numbering; none is in the paper's evaluation window. They are not used as angular
queries. The converter saves the retained base ID map in `base_original_ids.npy`.
Converted files retain the loader names
`sift_base.fvecs` and `sift_query.fvecs` for every dataset. Source/file/payload
hashes, dimensions and preprocessing are saved in `provenance.json`. Conversion
refuses existing nonempty output directories and nonfinite coordinates. Exact
ground truth is computed on the actual selected base subset, not copied from
an incompatible full-dataset neighbor list.

## Parameters

| Table 3 QVANN | N | M | efConstruction | E | Queries | Offset | Repeats | Construction threads |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| SIFT | 1,000,000 | 8 | 100 | 128 | 100 | 100 | 3 | 8 |
| NYTimes | 289,761 | 16 | 200 | 256 | 20 | 100 | 3 | 16 |
| GloVe | 1,183,514 | 16 | 200 | 1536 | 20 | 100 | 3 | 16 |
| DEEP | 1,000,000 | 16 | 200 | 64 | 20 | 100 | 3 | 16 |

All use k=10, SQ4 top-1 routing, SQ4/SQ6/raw refinement, BFS refinement ordering,
SHA-256 and shared multiproofs. Query and verification are single-threaded.
`scripts/run.py` remains a convenience launcher; its defaults are not the Table 3
protocol. Use `scripts/reproduce.py` for the paper configurations.

Faiss fixes the level RNG at 12345, an auxiliary HNSW generator at 456, and the
insertion shuffle at 789. These are not controlled by the legacy manifest field
`seed=42`. Multithreaded graph construction can still vary due to scheduling.
Exact historical graph fingerprints are recorded in `configs/paper.json`; those
indexes are not distributed. To require them, place the original hash-named
indexes under `cache/{dataset}/` and pass `--require-historical-index` to the
Table 3 runner. Otherwise a newly constructed graph is measured honestly, with
its checksum recorded; it is not claimed to reproduce identical paper numbers.

Figure 16 uses SIFT1M, M in {8,16,32,64}, efConstruction=200, k=10, queries
100–119, three repeats, and eight construction threads. For each M, the original
search-only coarse-to-integer calibration selects E closest to 95% recall, ties
preferring >=95% then smaller E. Calibration uses the measurement queries rather
than a held-out test set. The three variants share E, trace, answers and refinement
records. Each query/variant has one untimed warmup and rotated measurement order.

## Runs and output

```bash
python scripts/reproduce.py table3 --data-root data --output results/table3
python scripts/plot.py table3 --input results/table3/measurements.csv --output results/table3/plots
python scripts/reproduce.py figure16 --data-root data --output results/figure16
python scripts/plot.py figure16 --input results/figure16/measurements.csv --output results/figure16/plots
```

Table 3 exports aggregated measured rows to `table3.csv` and `table3.tex`. Figure 16 exports
`figure16.csv`, `figure16.pdf` and `figure16.png`. Plotting uses KB=1000 bytes and
ms; native `summary.csv` VO values use KiB=1024 bytes. Q is measured search plus
VO generation; V includes parsing, authentication, replay and result checking.
Network transport is excluded. No scaling factors, estimated values, stored
paper cells, or smoothed measurements are used.

Each run keeps native per-query CSVs, calibration CSVs, logs, actual arguments,
binary/input/graph checksums and a run manifest. `measurements.csv` provides a
common long-form schema with a relative raw source CSV and its SHA-256 per row.
The plotting script validates those raw-file hashes, rejects unverified,
duplicate or incomplete runs, and refuses to mix diagnostic and paper profiles.
Data and generated measurements stay local and are ignored by Git.

To exercise the pipeline on a real smaller dataset, prepare it with `--base 5000`,
then pass `--diagnostic-base 5000` to the runner. This selects two queries and one
repeat by default and labels all outputs `diagnostic`. Such results are not
paper measurements and Figure 16 plots visibly carry that label.

## Baselines and unresolved provenance

As requested for this source-only release, baseline implementations are external.
`configs/paper.json` records the manuscript's shared ANNProof graph parameters,
10,000-record Merkle shards and VLAH's 20,000-object bucket capacity. The final
per-dataset ANNProof efSearch, VLAH probing/LSH settings, MR-tree capacity, and
V3DB final adapted revision/IVF/PQ settings are not uniquely specified by the paper or a single
identified final run manifest. They remain explicit nulls, not inferred values.
The original V3DB upstream commit is recoverable and recorded separately; it
does not establish the final locally adapted execution revision.

A complete Table 3 comparison needs the original final baseline manifests and
matching external implementations. Do not substitute the QVANN budgets for
baseline search parameters. Once external methods are actually run, their raw
measurements can be normalized to the same `measurements.csv` schema with the
true method name, profile and raw CSV checksum; `plot.py table3 --input ... ...`
accepts multiple such files. The supplied command alone reproduces QVANN, not
the missing baseline rows.

## Validation limits

See `docs/validation.md` for the actual tested environment and cases. Fresh
source/dependency builds do not imply bit-identical hardware timing, historical
indexes, a container-isolated OS, or a complete rerun of the paper.
