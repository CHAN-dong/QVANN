# Baselines

The CMake build includes ANNProof, VLAH and MR-tree.

| Executable | Index and authentication | Hash |
|---|---|---|
| `annproof` | HNSW with sharded node and identifier Merkle trees | Keccak-256 |
| `vlah` | Gaussian LSH, Morton ordering, compressed trie and bucket-local HNSW | SHA-256 |
| `mr` | Packed high-dimensional R-tree, Merkle authentication and Ed25519 signatures | SHA-256 |

## Default parameters

All standalone launchers use k=10, 20 queries starting at row 100, three repetitions
and one construction/query thread. Parameters are in `configs/baselines.json`.

| Dataset | N | M | efConstruction |
|---|---:|---:|---:|
| SIFT | 1,000,000 | 8 | 100 |
| NYTimes | 289,761 | 16 | 200 |
| GloVe | 1,183,514 | 16 | 200 |
| DEEP | 1,000,000 | 16 | 200 |

M and efConstruction apply to ANNProof and VLAH.

| Method | Search and index parameters |
|---|---|
| ANNProof | efSearch=50; shard size=10,000; shared Merkle multiproofs |
| VLAH | efSearch=50; bucket capacity=20,000; probes=3; LSH width=256; LSH seed=42 |
| MR-tree | Exact kNN; fanout=16; leaf capacity=64; query offset=100 |

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

## Comparison

```bash
python scripts/compare.py --data-root data --output results/table3
python scripts/plot.py table3 --input results/table3/measurements.csv --output results/table3/plots
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
- Keccak-256: Project Nayuki's Bitcoin Cryptography Library; attribution is
  retained in `src/baselines/keccak.h` and `keccak.cpp`.
