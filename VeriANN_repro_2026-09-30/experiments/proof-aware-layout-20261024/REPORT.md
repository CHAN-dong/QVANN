# Proof-Cost-Aware Routing Merkle Layout: measured held-out experiment

**Implementation correction:** the existing QVANN routing tree is ID-major; only refinement trees are BFS. ID is therefore the actual original-routing control, and BFS is a newly applied routing layout. The experiment does not claim that routing BFS was previously implemented.

Each dataset: original million-vector graph, k10, native SQ4–SQ6–raw, SIFT E128 / DEEP E64; fixedseed20261024 split of original query files into256 training and100 independent test queries. Three timed repetitions, plus one untimed warmup per query/layout. Layouts frozen before any test traversal or test ground truth. No parameter was selected using test results. Native verifier receives only VO, trusted roots, query and codec metadata.

ID/Random/BFS keep each node’s per-level expansion records adjacent; Proof-Aware may rearrange individual records within bounded blocks. Targets are opened (node,level) expansion records, including upper layers, rather than just unique expanded node IDs. Four predetermined MinHash membership block sweeps propose arrangements; every acceptance must strictly decrease actual training multiproof sibling count. The fast objective is checked against Tree::prove for all train/layout pairs; all test queries generate and verify genuine multiproofs.

Routing proof bytes=4+32×sibling hashes. Every layout transmits the same existing4-byte leaf position per target; metadata is included in Total VO. Total authentication proof bytes include all three count-prefixed proofs; the CSV additionally reports authentication proofs+route position bytes. Identical nonrouting payload length and identical refinement proof bytes are asserted for every query. Total VO differences are therefore exactly routing proof differences.

## SIFT

Recall is identical across all four layouts: 96.50%; candidates 1287.50; targets 135.35. BFS→Proof-Aware: siblings 1072.83→1082.36, route proof 33.53→33.83KiB, total VO 213.15→213.44KiB, verification 4.40→4.41ms.

Reductions: sibling hashes -0.89%; routing proof -0.89%; total VO -0.14%; verification time -0.40% (negative means regression). Training hash reduction 64.63% versus held-out -0.89%. Held-out query wins/losses/ties in proof hashes: 26/72/2; median -1.03%, worst -5.67%, best 2.54%. Verification query wins/losses after averaging three repeats: 49/51.

Extra optimization+route-tree construction versus BFS 0.53s; training trace collection 0.26s separately. Serving mapping overhead versus BFS: 0 bytes. Illustrative client CPU break-even no observed saving queries, excluding network/trace cost; not an end-to-end deployment cost model.

BFS already cuts sibling hashes by 24.45% versus ID and 31.68% versus Random on independent queries. This supports useful graph-induced locality. Full verification reduction paired-query bootstrap95% interval: [-1.74%, 0.71%],10000 resamples of100 query pairs. This interval is conditional on this run and does not cover machine-to-machine or run-to-run systematic variation.

All four routing trees contain 1,142,701 real expansion-record leaves and occupy 128.00MiB of SHA256 nodes (same padded tree shape). BFS and Proof-Aware each need 4.36MiB for the serving record-ordinal→leaf-position map, hence0 extra index bytes versus BFS. Reused BFS initialization is 0.41s, actual tree construction BFS 0.59s / Proof-Aware 0.59s; optimization alone 0.53s. Proof-Aware ordering_ms=0 in construction.csv denotes reuse of the already constructed BFS seed, NOT zero standalone BFS construction cost. The small seed-vector copy/inversion is outside optimization_ms; this is not an exhaustive end-to-end fresh-index timer.

## DEEP

Recall is identical across all four layouts: 95.10%; candidates 1207.10; targets 69.18. BFS→Proof-Aware: siblings 647.65→654.04, route proof 20.24→20.44KiB, total VO 245.94→246.14KiB, verification 5.76→5.78ms.

Reductions: sibling hashes -0.99%; routing proof -0.99%; total VO -0.08%; verification time -0.32% (negative means regression). Training hash reduction 45.05% versus held-out -0.99%. Held-out query wins/losses/ties in proof hashes: 33/62/5; median -0.92%, worst -7.57%, best 4.17%. Verification query wins/losses after averaging three repeats: 57/43.

Extra optimization+route-tree construction versus BFS 0.37s; training trace collection 0.21s separately. Serving mapping overhead versus BFS: 0 bytes. Illustrative client CPU break-even no observed saving queries, excluding network/trace cost; not an end-to-end deployment cost model.

BFS already cuts sibling hashes by 23.03% versus ID and 23.23% versus Random on independent queries. This supports useful graph-induced locality. Full verification reduction paired-query bootstrap95% interval: [-1.87%, 1.32%],10000 resamples of100 query pairs. This interval is conditional on this run and does not cover machine-to-machine or run-to-run systematic variation.

All four routing trees contain 1,066,533 real expansion-record leaves and occupy 128.00MiB of SHA256 nodes (same padded tree shape). BFS and Proof-Aware each need 4.07MiB for the serving record-ordinal→leaf-position map, hence0 extra index bytes versus BFS. Reused BFS initialization is 0.67s, actual tree construction BFS 0.59s / Proof-Aware 0.59s; optimization alone 0.37s. Proof-Aware ordering_ms=0 in construction.csv denotes reuse of the already constructed BFS seed, NOT zero standalone BFS construction cost. The small seed-vector copy/inversion is outside optimization_ms; this is not an exhaustive end-to-end fresh-index timer.

## Judgment

The evidence does NOT support presenting this lightweight Proof-Aware layout as a strong, stable new QVANN contribution: assess the reported limited total-VO/time benefits and held-out regressions rather than training-objective gains. Keep it as an exploratory ablation; do not replace the default on this evidence.

BFS locality is assessed against both ID and fixed-seed random in summary.csv. Any hash reduction is entirely explained by changed shared-path locality, not fewer records or changed recall. Verification includes unaffected record hashing, refinement verification and traversal replay; proof-byte gains do not imply proportional full verification speedup. Timing effects are descriptive and can include cache/OS noise despite rotated order and warmups. Three repeats are not three independent queries. This is one fixed held-out split, not a multiple-seed stability result.

Construction CSV separates common codec/native-ADS construction, cached graph loading, each actual routing tree construction and layout optimization. Common ADS construction already contains the original ID route tree; do not sum it again to claim full fresh index construction. Route leaves/digests and tree shape are identical-sized; non-ID serving map uses4 bytes per route record. Optimization traces/membership and inverse order can be discarded online. Mapping files saved for auditing include ID too, although ID positions can be implicit.

Reproduce (from inner project): `wsl -e /root/.venvs/v3db-work9/bin/python scripts/run_proof_aware_layout_20261024.py all --run-id proof-aware-layout-20261024-rerun`. Original sources, graph caches and workbook are not modified. New files: src/experiments/proof_aware_layout.cpp and scripts/run_proof_aware_layout_20261024.py; a generated serializer changes only routing-position lookup and proof tree. build.json contains the exact compiler command and source/binary hashes. protocol.json and per-dataset split.json provide every parameter and query ID. Raw samples, training/test traces, costs, optimizer iteration history, full position permutations, sample VOs and roots are under results/<run-id>/.
