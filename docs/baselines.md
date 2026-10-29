# Baselines

The CMake build includes ANNProof, VLAH and MR-tree.

| Executable | Index and authentication | Hash |
|---|---|---|
| `annproof` | HNSW with sharded node and identifier Merkle trees | SHA-256 |
| `vlah` | Gaussian LSH, Morton ordering, compressed trie and bucket-local HNSW | SHA-256 |
| `mr` | Packed high-dimensional R-tree, Merkle authentication and Ed25519 signatures | SHA-256 |

## Launch defaults

Launchers use k=10, query offset=100, three repetitions and one
construction/query thread. All dataset uses 100 queries. Parameters are in `configs/baselines.json`.

| Dataset | N | M | efConstruction |
|---|---:|---:|---:|
| SIFT | 1,000,000 | 8 | 100 |
| NYTimes | 289,761 | 16 | 200 |
| GloVe | 1,183,514 | 16 | 200 |
| DEEP | 1,000,000 | 16 | 200 |
| OpenAI-ArXiv | 2,321,096 | 16 | 200 |

M and efConstruction apply to ANNProof and VLAH.

| Method | Search and index parameters |
|---|---|
| ANNProof | Shard size=10,000; shared Merkle multiproofs |
| VLAH | Bucket capacity=20,000; LSH seed=42 |
| MR-tree | Exact kNN; fanout=16; leaf capacity=64; query offset=100 |

Dataset search settings:

| Dataset | ANNProof efSearch | VLAH efSearch | VLAH probes | VLAH LSH width |
|---|---:|---:|---:|---:|
| SIFT | 100 | 50 | 41 | 256 |
| NYTimes | 264 | 400 | 14 | 0.3508760872900476 |
| GloVe | 1536 | 200 | 56 | 0.33011345496943795 |
| DEEP | 64 | 100 | 44 | 0.3456967236059823 |
| OpenAI | 512 | 50 | 46 | 0.17173320405561338 |

VLAH uses eight Gaussian projections with biased 8-bit coordinates interleaved
into a 64-bit Morton key. MR-tree groups records by recursive spatial packing.

## Commands

```bash
python scripts/baseline.py annproof --dataset data/sift --preset sift --output results/annproof
python scripts/baseline.py vlah --dataset data/sift --preset sift --output results/vlah
python scripts/baseline.py mr --dataset data/sift --preset sift --output results/mr
```

Override search parameters explicitly:

```bash
python scripts/baseline.py annproof --dataset data/sift --preset sift \
  --ef-search 128 --shard-size 10000 --output results/annproof-128
python scripts/baseline.py vlah --dataset data/sift --preset sift \
  --ef-search 100 --probes 10 --seed 42 --output results/vlah-p10
python scripts/baseline.py mr --dataset data/sift --preset sift \
  --leaf-capacity 128 --output results/mr-leaf128
```

`--base`, `--queries`, `--repeats` and `--k` select the workload. Use a separate
output directory for each run. The raw native CSV and unified `measurements.csv`
are saved together with the command and binary/input checksums.

## Overall comparison (Table 4)

```bash
python scripts/compare.py --data-root data --output results/table4
python scripts/plot.py table4 --input results/table4/measurements.csv --output results/table4/plots
```

The comparison reads QVANN's workload from `configs/paper.json` and baseline
search settings from `configs/baselines.json`. All methods use the same dataset,
query window, k and repetition count. QVANN and ANNProof use the same graph cache;
VLAH stores its bucket indexes separately. The CSV has separate `E`, `efSearch`,
`probes` and `leaf_capacity` fields.

## Implementation references

- ANNProof: graph authentication with separate node and returned-identifier commitments.
- VLAH: LSH bucket routing, authenticated trie descriptors and local graph verification.
- MR-tree: Chen, Hu and Xu, *Authenticating Top-k Queries in Location-based Services
  with Confidentiality*, PVLDB 7(1), 2013, Section 5.1.1. The included tree uses
  packed spatial construction with fanout 16.
