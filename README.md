# QVANN

**A Quantization-Driven Authenticated Index for Efficient Verifiable ANN Search**

QVANN combines SQ4 graph traversal, progressive SQ4/SQ6/raw certification, and expansion-centric Merkle authentication. Clients verify search execution and returned results using verification objects and trusted index commitments.

## Build

Linux x86-64/WSL, C++17, CMake 3.18+, OpenSSL 1.1.1+, OpenMP, OpenBLAS and Python 3.

```bash
git clone https://github.com/CHAN-dong/QVANN.git
cd QVANN
bash scripts/install.sh
python3 scripts/deps.py --jobs 2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DQVANN_DEPS_DIR="$PWD/.deps"
cmake --build build -j2
python3 scripts/smoke.py
```

The build produces `qvann`, `annproof`, `vlah`, `mr`, and `granularity`. Dependencies are pinned in `deps/versions.json`; installation details are in [docs/reproduce.md](docs/reproduce.md).

## Data

Install the data-conversion and plotting packages:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python scripts/prepare.py --dataset sift \
  --source /downloads/base.1M.prefix.u8bin \
  --queries /downloads/query.public.10K.u8bin --output data/sift
```

Prepare SIFT, NYTimes, GloVe, DEEP and OpenAI under `data/{sift,nytimes,glove,deep,openai}/`. Each directory contains `sift_base.fvecs` and `sift_query.fvecs`. Download links and preprocessing steps are in the [data guide](docs/reproduce.md#data-preparation).

**OpenAI-ArXiv** contains 2,321,096 arXiv abstract embeddings generated with OpenAI's ada-002 model, with 1536 dimensions and 20,000 queries. The evaluation uses the full base dataset, preserving float32 coordinates and squared L2 distance. Prepare the [BigANN binary files](docs/reproduce.md#openai-arxiv) with:

```bash
python scripts/prepare.py --dataset openai --source /downloads/openai_base.bin \
  --queries /downloads/openai_query.bin --output data/openai
```

## Run

```bash
python scripts/run.py --dataset data/sift --preset sift --output results/qvann
python scripts/baseline.py annproof --dataset data/sift --preset sift --output results/annproof
python scripts/baseline.py vlah --dataset data/sift --preset sift --output results/vlah
python scripts/baseline.py mr --dataset data/sift --preset sift --output results/mr
```

Baseline defaults are in [configs/baselines.json](configs/baselines.json). Settings are k=10, query offset=100 and 3 repetitions, with 100 queries for SIFT and 20 for the other datasets. See [baseline parameters and commands](docs/baselines.md).

## Experiments

Overall performance (Table 4) and authentication granularity (Figure 16):

```bash
python scripts/compare.py --data-root data --output results/table4
python scripts/plot.py table4 --input results/table4/measurements.csv --output results/table4/plots
python scripts/reproduce.py figure16 --data-root data --output results/figure16
python scripts/plot.py figure16 --input results/figure16/measurements.csv --output results/figure16/plots
```

`compare.py` runs QVANN, ANNProof, VLAH and MR-tree. `--datasets sift,deep` selects datasets; `--methods qvann,annproof` selects methods. Figure 16 evaluates Record-wise, Node-wise and ER authentication. Configurations are in `configs/paper.json` and `configs/baselines.json`.

Outputs include per-query CSVs, run parameters, input/index checksums, summary tables and figures. Timing uses ms and plotted VO sizes use KB=1000 bytes.

## Layout

`src/` contains the index, optimization, query, verification and baseline implementations. `scripts/` provides data preparation and experiment entry points; `tests/` provides query and integrity checks. See [build and test results](docs/validation.md).
