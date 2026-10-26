# Impact of Routing Precision — measured results (2026-10-24)

All rows use the original fixed graph per dataset, query IDs100..119, k10,20 queries×3 repetitions and one thread. E is a unique-expanded-node budget, not Faiss efSearch. KB=1000bytes. Calibration enumerates every integer E1..256 on the same20 queries, so matched recall is not a held-out accuracy claim.

Both modes share precisely the same trusted route/delta/raw Merkle roots and final progressive result rules. Exact authenticates all raw vectors used by precise upper-layer/frontier decisions, transmits each once, and reuses computed exact distances for final ranking. SQ4 is byte-identical to original QVANN after removing a common4-byte mode tag. There is no server graph/database at the client.

SIFT / Fixed E: Exact E=128, R=98.00%, C=1159.95, VO=968.35KB, V=13.07ms; SQ4 E=128, R=98.00%, C=1152.05, VO=202.24KB, V=4.35ms. SQ4 changes recall by +0.00pp and candidates by -0.68%; VO reduction=79.11%, V reduction=66.75%.

SIFT / Matched R: Exact E=88, R=95.00%, C=833.25, VO=716.68KB, V=9.80ms; SQ4 E=111, R=95.00%, C=1013.45, VO=185.00KB, V=4.05ms. SQ4 changes recall by +0.00pp and candidates by +21.63%; VO reduction=74.19%, V reduction=58.71%.

DEEP / Fixed E: Exact E=64, R=96.00%, C=1230.35, VO=941.25KB, V=15.89ms; SQ4 E=64, R=97.50%, C=1233.80, VO=271.09KB, V=6.73ms. SQ4 changes recall by +1.50pp and candidates by +0.28%; VO reduction=71.20%, V reduction=57.64%.

DEEP / Matched R: Exact E=56, R=95.00%, C=1095.15, VO=849.50KB, V=14.29ms; SQ4 E=49, R=95.00%, C=986.70, VO=231.40KB, V=5.65ms. SQ4 changes recall by +0.00pp and candidates by -9.90%; VO reduction=72.76%, V reduction=60.46%.

Routing scores can change the expansion trajectory and candidate count at fixed E; they do not imply equal recall. Matching measured recall permits different E. Communication differences include all required routing disclosure and raw-tree authentication; they are not payload-only estimates. Verification time includes decoding, all Merkle checks, actual requested-precision replay and final ranking/certificate checks. Values above are preserved whether or not a hypothesis is supported.

Fixed E deliberately retains SIFT128/DEEP64 from current defaults; the20-query subset can have higher recall than the historical SIFT100-query mean. Each recall step is0.5 percentage points across20 independent top10 queries. Repeated queries do not increase the independent accuracy sample size.

Reproduce: `wsl -e /root/.venvs/v3db-work9/bin/python scripts/run_routing_precision_20261024.py all`, then `wsl -e /root/.venvs/v3db-work9/bin/python scripts/audit_routing_precision_20261024.py`.

New code: `src/experiments/routing_precision.cpp`, `scripts/run_routing_precision_20261024.py`, and `scripts/audit_routing_precision_20261024.py`. The original QVANN source, datasets, graph caches, workbook and previous experiments are not modified. Detailed protocol, compilation/source/binary hashes, chosen E, raw query records, exact expansion scores, complete sample VOs, semantic rejection witnesses and analyses are stored with this study.
