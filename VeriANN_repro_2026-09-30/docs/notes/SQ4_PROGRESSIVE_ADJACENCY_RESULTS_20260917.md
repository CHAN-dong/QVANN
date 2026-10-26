# SQ4-aware progressive adjacency scan

Complete scan: 3 primary selectors x 5 DP x 3 DR x 4 E = 180 configurations,
100 SIFT100K queries. Upper HNSW layers and final SQ4-to-SQ6-to-raw verification are
unchanged. Residual opening is deterministically triggered by local SQ4 stall.

Only eight configurations reach Recall at least 0.99, and all require E=128. The best
matched-recall point is robust pruning with DP=10, DR=12: Recall 0.991, disclosed
930.61, fallback 17.58%, raw 22.68, adjacency 5.83 KiB, total 86.63 KiB, and 3.20 ms.
It is worse than the E=64 baseline (Recall 0.992, disclosed 811.86, VO 76.01 KiB).

The same robust DP=10, DR=12 policy gives the intended communication range at lower E,
but loses Recall: E=64 is Recall 0.956 / 547.07 disclosed / 56.19 KiB; E=80 is
0.968 / 649.06 / 64.35 KiB; E=96 is 0.973 / 745.36 / 71.94 KiB.

At E=64 the best Recall per selector is 0.965 for top-D, 0.973 for robust, and 0.964
for neighbor-cover. Fallback is not the bottleneck; the matched point opens residual
at only 17.58% of level-0 expansions. The primary graph loses navigation efficiency,
so doubling E is needed and erases the disclosure savings. This direction is stopped.
