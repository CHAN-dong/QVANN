# QVANN

**A Quantization-Driven Authenticated Index for Efficient Verifiable ANN Search**

QVANN combines SQ4 graph traversal, progressive SQ4/SQ6/raw certification, and expansion-centric Merkle authentication to reduce verification-object size. Verification certifies the prescribed ANN protocol over a committed index, rather than exact global nearest neighbors.

## Build

Linux x86-64/WSL, C++17, CMake 3.18+, OpenSSL 1.1.1+, OpenMP, OpenBLAS and Python 3 are required. The dependency builder pins the modified Faiss 1.5.2 fork and LightGBM 2.3.1 to exact commits, applies the recorded patch, and compiles both from source.

```bash
git clone https://github.com/CHAN-dong/QVANN.git
cd QVANN
bash scripts/install.sh                 # Debian/Ubuntu prerequisites; needs sudo unless root
python3 scripts/deps.py --jobs 2        # Builds into .deps/
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DQVANN_DEPS_DIR="$PWD/.deps"
cmake --build build -j2
python3 scripts/smoke.py
```

For data conversion and plotting:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

`requirements.lock` records the tested Python 3.14 environment. See [dependency and data details](docs/reproduce.md) for pinned sources and custom install locations.

## Data and queries

Download the real SIFT, NYTimes, GloVe or DEEP inputs using the [data instructions](docs/reproduce.md#data-preparation). Convert each into `data/{sift,nytimes,glove,deep}/`:

```bash
python scripts/prepare.py --dataset sift \
  --source /downloads/sift-128-euclidean.hdf5 --output data/sift
python scripts/run.py --dataset data/sift --preset sift --output results/example
```

The converter records source checksums and preprocessing. It preserves the original NYTimes query numbering. Inputs, cached indexes and generated results remain local.

## Paper experiments

[configs/paper.json](configs/paper.json) records the paper parameters, query windows, seeds and historical input/index fingerprints. Use these commands instead of the convenience launcher's defaults:

```bash
python scripts/reproduce.py table3 --data-root data --output results/table3
python scripts/plot.py table3 --input results/table3/measurements.csv --output results/table3/plots
python scripts/reproduce.py figure16 --data-root data --output results/figure16
python scripts/plot.py figure16 --input results/figure16/measurements.csv --output results/figure16/plots
```

Table 3 reproduces **QVANN's row**; external baseline code is not bundled. Unresolved final baseline parameters are explicit in the configuration and [reproduction notes](docs/reproduce.md#baselines-and-unresolved-provenance). Figure 16 runs Record-wise, Node-wise and ER authentication with the original shared-trace and integrity checks.

Runs preserve native per-query CSVs, calibration, logs, arguments and input/binary/index checksums. `measurements.csv` unifies measured samples; plotting validates their raw CSV hashes. Paper output uses KB=1000 bytes and ms, with no estimates or scaling factors. New graphs can differ from historical indexes; `--require-historical-index` enforces the recorded Table 3 graph fingerprints.

## Layout and validation

`src/` contains `index`, `optimize`, `query`, `verify`, `common` and `utils`; `src/experiments` holds the original authentication ablation. Build configuration, tests, scripts and paper settings are separate.

See [actual validation and limitations](docs/validation.md) and the [retained diagnostic CSVs](validation/README.md). The core ANN algorithm and existing result files are unchanged. Full paper-scale timing and baseline comparisons are not claimed by the smaller diagnostic runs.
