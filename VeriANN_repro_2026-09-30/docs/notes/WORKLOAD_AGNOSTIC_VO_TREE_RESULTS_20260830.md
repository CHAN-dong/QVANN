# Workload-agnostic VO tree experiment

Date: 2026-08-30

## Scope

- Dataset: first 100,000 SIFT base vectors, first 1,000 test queries, k=100.
- Baseline: 1,024 coarse groups -> 65,536 residual fine leaves, SQ4 coarse and PQ24 residual routing.
- Verifier: stateless; codebooks, routing streams, metadata, SHA/Merkle multiproofs, and leaf/object certificates are charged per query.
- Recall uses exact top-100 recomputed on SIFT100K.
- ANN time includes routing plus exact candidate scan. It does not include real Huffman serialization, SHA-256 execution, signature verification, or network transfer.

The new topology is data-aware but workload-agnostic: no query trace or node access probability is used to build the balanced coarse-to-fine groups.

## Implemented query rule

For an authenticated child block u with decoded representative c, covering radius rho, and optional PQ symbol sets, the verifier computes a conservative lower bound. Blocks are visited globally in lower-bound order. Once the current fine top-768 threshold is no greater than every unopened lower bound, routing stops.

Thus the number of opened blocks is query-adaptive. The fixed top-768 is the output cardinality of fine routing, not a fixed number of blocks per parent. The implementation verifies that the selected fine set exactly matches exhaustive residual routing.

## Iteration 1: existing unbalanced authenticated blocks

A 48-center ball block preserves the same fine selection and Recall, but only removes about 70 of roughly 11,802 residual codes per query. Its authenticated descriptors make total VO 6.90% larger on the 100-query recheck.

## Iteration 2: balanced coarse-to-fine topology

The original hierarchical k-means can produce highly unequal coarse groups. A capacity-balanced split was added: each split learns two centers, ranks fine centers by relative distance, and cuts the ranking in half. With 1,024 groups and 65,536 fine leaves, every coarse group contains 64 fine centers.

This reduces the residual candidates at coarse_probe=160 from 11,802.218 to exactly 10,240 without using a query workload.

### 1,000-query results

| topology | coarse probe | Recall@100 | candidate residuals | fixed-code VO | Huffman VO | Huffman routing | leaf VO | ANN ms | VO-generation ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| original unbalanced | 160 | 0.99073 | 11,802.218 | 583.81 KiB | 561.08 KiB | 426.60 KiB | 134.48 KiB | 3.587 | 1.573 |
| balanced | 160 | 0.98882 | 10,240 | 547.52 KiB | 527.91 KiB | 391.42 KiB | 136.49 KiB | 3.637 | 1.651 |
| balanced | 172 | 0.99013 | 11,008 | 566.68 KiB | 546.53 KiB | 410.01 KiB | 136.52 KiB | 3.647 | 1.572 |
| balanced boundary | 173 | 0.99023 | 11,072 | 568.20 KiB | 548.01 KiB | 411.51 KiB | 136.50 KiB | 3.594 | 1.724 |
| balanced robust | 176 | 0.99055 | 11,264 | 572.94 KiB | 552.60 KiB | 416.09 KiB | 136.51 KiB | 3.730 | 1.607 |

All runs have zero object-bound violations.

At the historical Recall threshold 0.99022, coarse_probe=173 is the smallest tested passing point. Compared with the previous Huffman result it reduces total VO by 2.33% and changes ANN latency by about +0.2%, but its Recall margin is only 0.00001. The safer coarse_probe=176 point reduces VO by 1.51%, with a roughly 4.0% latency increase and a 0.00033 margin over the threshold.

## Iteration 3: dynamic lower-bound routing

The balanced tree was tested with one 64-center block per coarse group and with two 32-center child blocks.

| dynamic structure, 100 queries | Recall@100 | opened / possible fine codes | total VO | matching static baseline | VO change | ANN time |
|---|---:|---:|---:|---:|---:|---:|
| 64-center root ball, no PQ masks | 0.9930 | 10,240 / 10,240 | 526.87 KiB | 521.92 KiB | +0.95% | 4.88 vs 3.70 ms |
| 32-center child balls, no PQ masks | 0.9936 | 11,000.98 / 11,072 | 593.56 KiB | 541.90 KiB | +9.53% | 4.24 vs 3.55 ms |
| 32-center blocks, 32 PQ mask subspaces | 0.9936 | 10,744.70 / 11,072 | 837.55 KiB | 541.90 KiB | +54.56% | 6.83 vs 3.55 ms |

All dynamic runs have zero selection mismatches and zero lower-bound violations. Stronger PQ-set bounds prune more codes, but the mask and authenticated descriptor bytes grow much faster than the saved residual payload.

## Workload-agnostic cost decision

For a query q, charge actual bytes rather than node counts:

V(q) = static parameters + expanded descriptors + opened routing codes + multiproofs + leaf certificates.

At an internal node v, compare:

- raw(v): reveal all routing codes below v;
- split(v): reveal authenticated child descriptors, then recursively open only children whose lower bounds survive.

A robust query-independent builder should keep a split only when it can beat raw(v) under an explicit worst-case or geometric-overlap bound. At query time, a public cost guard can choose the smaller valid transcript, raw or bounded-tree. On SIFT100K the measured guard chooses the raw balanced slices; the dynamic bound tree is not retained.

This also shows why VO alone cannot determine top counts. Without a correctness constraint it chooses zero work. Exact authenticated lower bounds make the number of expanded nodes emerge from the query. If approximate Recall@100 is used instead, selecting coarse_probe still requires a declared query distribution or benchmark because Recall itself is distributional.

## Applicability

The balanced hierarchy is useful when residual-slice bytes dominate and ordinary clustering creates uneven child populations. It is simple, data-only, and preserves millisecond-scale routing.

The authenticated dynamic-bound tree is more likely to help when:

- dimension and cluster overlap are low;
- a pruned subtree contains many routing codes;
- child summaries are compact or cached;
- queries have tight result radii;
- the authentication primitive amortizes child commitments better than per-child SHA records.

It is a poor fit here because 128-D residual balls overlap heavily, each coarse group has only 64 fine centers, and a stateless verifier pays summary and commitment bytes on every query.

## Recommendation

Use the balanced two-level residual hierarchy, not the dynamic ball/PQ block tree. Use coarse_probe=176 as the conservative configuration (Recall 0.99055, 552.60 KiB Huffman VO, 3.73 ms), or coarse_probe=173 only when the benchmark threshold 0.99022 is authoritative (Recall 0.99023, 548.01 KiB, 3.59 ms).

The result is currently specific to SIFT100K and one deterministic build. A production claim still needs SIFT1M plus at least one different geometry dataset, multiple build seeds, serialized Huffman round trips, and end-to-end cryptographic latency.
