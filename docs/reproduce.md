# Build and experiments

## Dependencies

Run `bash scripts/install.sh` and `python3 scripts/deps.py --jobs 2` on
Debian/Ubuntu x86-64. The build uses SSE4/POPCNT, OpenBLAS, OpenMP and OpenSSL.

| Dependency | Source version |
|---|---|
| Modified Faiss | 1.5.2; `efficient/faiss-learned-termination` commit `ec2e252e1a316f5a3cb2e108082507ccbfa29ed1` plus `deps/faiss.patch` |
| LightGBM | 2.3.1; commit `471d0aaaaa8574b9a1da2302e992f61b01e3d155` |
| CMake | 3.18+ |
| OpenSSL | 1.1.1+ |
| Compiler | C++17 |

The installer builds both libraries and writes source/compiler/library checksums
to `.deps/build.json`. Use `scripts/deps.py --prefix /path/to/deps` and configure
with `-DQVANN_DEPS_DIR=/path/to/deps` to select another dependency directory.
`requirements.lock` lists the tested Python 3.14 packages; `requirements.txt`
provides package ranges for data conversion and plotting.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DQVANN_DEPS_DIR="$PWD/.deps"
cmake --build build -j2
python3 scripts/smoke.py
```

Set `-DQVANN_BUILD_BASELINES=OFF` to build only QVANN and its tests. Baseline
executables share the same dependency installation. Keccak is compiled from the
included source. MR-tree links OpenSSL directly.

## Data preparation

| Dataset | Download | Size / dimension | Preprocessing |
|---|---|---|---|
| SIFT | [HDF5](https://ann-benchmarks.com/sift-128-euclidean.hdf5) | 1,000,000 / 128 | Float32 |
| NYTimes | [HDF5](https://ann-benchmarks.com/nytimes-256-angular.hdf5) | 289,761 / 256 | Remove zero base rows; unit normalization |
| GloVe | [HDF5](https://ann-benchmarks.com/glove-100-angular.hdf5) | 1,183,514 / 100 | Unit normalization |
| DEEP | [Base](https://storage.yandexcloud.net/yandex-research/ann-datasets/DEEP/base.1B.fbin), [queries](https://storage.yandexcloud.net/yandex-research/ann-datasets/DEEP/query.public.10K.fbin) | 1,000,000 / 96 | First 1M base rows; float32 |
| OpenAI-ArXiv | [Base](https://comp21storage.z5.web.core.windows.net/arxiv-openaiv2-2M/openai_base.bin), [queries](https://comp21storage.z5.web.core.windows.net/arxiv-openaiv2-2M/openai_query.bin) | 2,321,096 / 1536 | Full base dataset; float32; squared L2 |

```bash
python scripts/prepare.py --dataset sift --source /downloads/sift-128-euclidean.hdf5 --output data/sift
python scripts/prepare.py --dataset nytimes --source /downloads/nytimes-256-angular.hdf5 --output data/nytimes
python scripts/prepare.py --dataset glove --source /downloads/glove-100-angular.hdf5 --output data/glove
python scripts/prepare.py --dataset deep --source /downloads/base.1M.prefix.fbin \
  --queries /downloads/query.public.10K.fbin --output data/deep
```

The DEEP first-million prefix occupies bytes 0–384000007 of the base fbin file:

```bash
curl -fL --retry 3 --range 0-384000007 --max-filesize 384000008 \
  'https://storage.yandexcloud.net/yandex-research/ann-datasets/DEEP/base.1B.fbin' \
  -o base.1M.prefix.fbin
```

Check HTTP 206 and the matching Content-Range. Normalization uses float64 norms
and float32 reciprocal scaling. NYTimes preserves query row numbers, including
its nine zero query rows outside the selected query window. The retained base ID
map is saved as `base_original_ids.npy`.

Each prepared directory contains `sift_base.fvecs`, `sift_query.fvecs` and
`provenance.json`. An fvecs record contains a little-endian int32 dimension and
float32 coordinates. The program computes ground truth for the selected base
and query rows.

### OpenAI-ArXiv

The dataset contains OpenAI ada-002 embeddings of arXiv paper abstracts:
2,321,096 base vectors, 1536 dimensions, and 20,000 queries. It is provided by
[BigANN's OpenAIArXivDataset](https://github.com/harsha-simhadri/big-ann-benchmarks/blob/main/benchmark/datasets.py).
The benchmark identifier is `openai-2M`. Each binary file starts with two
little-endian int32 values, N and D, followed by contiguous float32 vectors.
Preparation preserves the coordinates and dimension. Distance is squared L2.

```bash
curl -fL --retry 3 https://comp21storage.z5.web.core.windows.net/arxiv-openaiv2-2M/openai_base.bin -o openai_base.bin
curl -fL --retry 3 https://comp21storage.z5.web.core.windows.net/arxiv-openaiv2-2M/openai_query.bin -o openai_query.bin
python scripts/prepare.py --dataset openai --source openai_base.bin \
  --queries openai_query.bin --output data/openai
python scripts/run.py --dataset data/openai --base 2321096 --M 16 \
  --ef-construction 200 --output results/openai
```

Use `--budget` to select QVANN's expansion budget. The prepared OpenAI input also
works with `scripts/baseline.py --preset openai`, for example:

```bash
python scripts/baseline.py annproof --dataset data/openai --preset openai --output results/annproof-openai
```

## QVANN parameters

| Dataset | N | M | efConstruction | E | Queries | Offset | Repeats |
|---|---:|---:|---:|---:|---:|---:|---:|
| SIFT | 1,000,000 | 8 | 100 | 128 | 100 | 100 | 3 |
| NYTimes | 289,761 | 16 | 200 | 256 | 20 | 100 | 3 |
| GloVe | 1,183,514 | 16 | 200 | 1536 | 20 | 100 | 3 |
| DEEP | 1,000,000 | 16 | 200 | 64 | 20 | 100 | 3 |

QVANN uses k=10, SQ4 top-1 routing, SQ4/SQ6/raw refinement, BFS refinement ordering
and SHA-256 shared multiproofs. Query and verification use one thread. E counts
unique expanded nodes. Faiss's level, auxiliary and insertion generators use
seeds 12345, 456 and 789. Graph checksums are recorded for index reuse.

Baseline defaults and overrides are listed in [baselines.md](baselines.md).

## Experiment commands

```bash
python scripts/compare.py --data-root data --output results/table3
python scripts/plot.py table3 --input results/table3/measurements.csv --output results/table3/plots
python scripts/reproduce.py figure16 --data-root data --output results/figure16
python scripts/plot.py figure16 --input results/figure16/measurements.csv --output results/figure16/plots
```

Figure 16 uses SIFT1M, M={8,16,32,64}, efConstruction=200, k=10, query rows 100–119,
and three repeats. E is selected near 95% recall. Record-wise, Node-wise and ER
share traversal and refinement records. Measurement order rotates between
variants, with one warmup per query/variant.

For a smaller input prefix:

```bash
python scripts/compare.py --data-root data --datasets sift --base 5000 \
  --queries 2 --repeats 1 --output results/sift-small
```

## Outputs

Runs retain native per-query CSVs, selected parameters, logs and checksums.
`measurements.csv` combines methods using a common schema. Table output includes
`table3.csv` and `table3.tex`; figure output includes CSV, PDF and PNG.
Q includes search and VO generation. V includes parsing, authentication, replay
and result checking. Times are in ms, chart sizes are in KB=1000 bytes, and native
KiB fields use 1024 bytes. CSV source hashes are checked when generating figures.
