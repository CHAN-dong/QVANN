# Verification-aware progressive quantization experiment

## Question

Can non-uniform nested boundaries, per-dimension bit allocation, and level
selection reduce the progressive verification cost when the training objective
is

```
VO bytes + lambda_bytes_per_ms * protocol latency_ms
```

## Protocols tested

- **U**: uniform nested scalar boundaries.
- **DQ/WQ**: data-quantile and workload-weighted quantile boundaries.
- **VT**: a train-only verification-aware binary partition tree.  Each nested
  split minimizes certified squared-distance slack for visited
  vector/query pairs near the final threshold.
- **VA**: fixed 512-bit first level with matched per-dimension upgrades and
  downgrades (5/3 bits instead of 4/4), learned on training queries.
- Level candidates: SQ6, 4->6, 4->5->6, 3->4->6, 5->6, and VA->6 variants.

The final protocol is selected on training queries by the objective above.
Testing uses disjoint queries.

## Main held-out result

SIFT100K, IVF16384, nprobe=640, k=100, 100 train queries, 100 disjoint test
queries, three timing repetitions:

| Protocol | Test VO | Exact vectors | Bound + proof time | End-to-end time |
|---|---:|---:|---:|---:|
| Uniform SQ6 | 689.46 KiB | 184.96 | 0.977 ms | 11.536 ms |
| Uniform 4->6 | 604.02 KiB | 184.96 | 1.369 ms | 11.928 ms |
| VT SQ6 | 681.39 KiB | 169.95 | 1.035 ms | 11.594 ms |
| **VT 4->6** | **583.34 KiB** | **169.95** | **1.368 ms** | **11.927 ms** |
| VT 4->5->6 | 609.58 KiB | 169.95 | 1.542 ms | 12.101 ms |
| VT VA2->6 (pure-VO best) | 581.14 KiB | 169.95 | 1.633 ms | 12.192 ms |

Recall is 0.9939 for every protocol because quantization only constructs the
certificate for the same IVF answer.  Numerical lower-bound checks found zero
violations.

Relative to uniform 4->6, VT 4->6:

- reduces VO by **3.42%** (20.68 KiB/query);
- reduces exact disclosures by **8.12%**;
- changes measured end-to-end latency by -0.002 ms (measurement noise);
- reduces modeled query + delivery time by 1.70 ms at 100 Mbps and 0.17 ms at
  1 Gbps.

The earlier 50/50 held-out run independently showed a 3.85% VO reduction and
9.19% fewer exact disclosures, so the boundary gain is repeatable.

## What worked

1. **Verification-aware boundaries worked.**  They optimize certificate
   tightness near the query threshold, not bin occupancy.  This is the main
   gain.
2. **Two levels (4->6) remained Pareto-best** at ordinary network speeds.
   Adding the 5-bit level increased VO by 4.50% and latency by 0.175 ms because
   its extra Merkle proof outweighed the saved exact payload.
3. **Training-time lambda selection generalized.**  It selected VT 4->6 at
   100 Mbps and 1 Gbps, and the held-out objective agrees.  At 10 Gbps it
   selected one-shot uniform SQ6; the approximate 4->6/SQ6 break-even is
   2.2 Gbps.

## What did not work

- Ordinary data/workload quantiles were worse than uniform boundaries; balanced
  occupancy is not the correct objective for a distance lower-bound
  certificate.
- Per-dimension allocation is not yet a strong contribution.  Its best
  held-out VO saving over VT 4->6 is only 0.38% (2.20 KiB), but it costs about
  0.266 ms.  It is worthwhile only below roughly 68 Mbps under this cost model,
  and is rejected at 100 Mbps and above.

## Scope and limitations

- Merkle bytes use exact multiproof hash-count accounting, but the reported
  proof time measures proof-layout/count construction, not full SHA-256
  generation, serialization, network transfer, and client verification.
- Codes are unpacked server-side in this prototype.  A production experiment
  must measure packed bitplane decoding.
- The current verification tree exploits SIFT's integer [0,255] coordinates.
  Continuous-valued datasets need candidate split compression or differentiable
  boundary learning.

## Reproduction

```bash
make vo_latency_quantization_experiment
./vo_latency_quantization_experiment \
  --dataset ../dataset/sift --base 100000 \
  --test-queries 100 --train-queries 100 \
  --nlist 16384 --nprobe 640 --repeats 3 \
  --output vo_latency_quantization_sift100k_vt_100q.csv
```

Implementation: `vo_latency_quantization.cpp`

