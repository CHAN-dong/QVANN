# Quantization VO comparison (2026-08-25)

## Setup

- Dataset: SIFT100K, 128 dimensions
- Index: hierarchical IVF, `nlist=16384`, `nprobe=640`, `k=100`
- Split: 100 training queries and 100 disjoint test queries
- VO accounting: object IDs, quantized/exact payload, local Merkle multiproofs,
  and routing proofs
- Average visited objects: 4,979.03/query
- Recall@100: 0.9939 for every scheme
- Certified lower-bound violations: 0

The no-quantization baseline returns every visited vector and its ID, plus the
same routing proof:

```text
raw VO = visited * (512-byte vector + 4-byte ID) + routing proof
       = 2,653,831.56 bytes/query
```

## Held-out comparison

| Scheme | Boundaries | Levels | Exact vectors/query | VO/query | Reduction vs raw | Incremental reduction |
|---|---|---:|---:|---:|---:|---:|
| No quantization | none | exact | 4,979.03 | 2,591.63 KiB | 0.00% | - |
| Ordinary quantization | uniform | SQ6 | 184.96 | 689.46 KiB | 73.40% | 73.40% vs raw |
| Trained quantization | verification tree | SQ6 | 169.95 | 681.39 KiB | 73.71% | 1.17% vs ordinary SQ6 |
| Trained multi-level | verification tree | SQ4 -> SQ6 | 169.95 | 583.34 KiB | 77.49% | 14.39% vs trained SQ6 |
| Optional dimension-aware multi-level | verification tree | VA1 -> SQ6 | 169.95 | 581.42 KiB | 77.57% | 0.33% vs VT SQ4 -> SQ6 |

Relative to ordinary uniform SQ6, the recommended trained SQ4 -> SQ6 scheme
reduces total VO by 15.39%.

## Interpretation

- Uniform SQ6 produces the dominant gain: it replaces most 512-byte vectors
  with 96-byte codes and reduces exact disclosures from 4,979.03 to 184.96.
- Verification-tree training reduces exact disclosures by another 8.12%, but
  the one-level VO reduction is only 1.17% because every visited object still
  carries a full SQ6 code.
- SQ4 -> SQ6 sends the 32-byte SQ6 refinement to only 558.51 of 4,979.03
  visited objects. It saves 141,456.79 payload bytes, pays 41,047.30 extra
  refinement-proof bytes, and therefore saves a net 100,409 bytes/query over
  trained one-level SQ6.
- Simple workload-weighted quantile boundaries are not the trained method used
  above: WQ-SQ6 measured 714.02 KiB (731,154 bytes), 3.56% worse than uniform
  SQ6. The effective training objective is verification slack near the top-k
  threshold.

Raw measurements are in `vo_quant_comparison_20260825.csv`.
