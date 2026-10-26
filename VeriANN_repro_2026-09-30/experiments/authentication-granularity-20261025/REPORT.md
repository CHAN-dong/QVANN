# Impact of Authentication Granularity — 实测结果

单位：KB=1000 bytes，时间 ms。全部为原始实测；20 条查询×3 次，每个 M 使用相同 E、图、查询、SQ4--SQ6--raw refinement。完整认证定义和复现命令见 README.md。

| Dataset | M | Variant | E | Recall (%) | Routing proof (KB) | Routing payload (KB) | Total VO (KB) | V (ms) | Q (ms) |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|
| SIFT | 8 | Record-wise | 93.00 | 95.00 | 324.35 | 69.70 | 460.76 | 13.77 | 4.07 |
| SIFT | 8 | Node-wise | 93.00 | 95.00 | 279.95 | 70.53 | 417.19 | 10.79 | 3.54 |
| SIFT | 8 | ER | 93.00 | 95.00 | 33.38 | 68.85 | 168.94 | 3.47 | 1.93 |
| SIFT | 16 | Record-wise | 33.00 | 95.00 | 198.31 | 42.37 | 295.40 | 8.02 | 2.16 |
| SIFT | 16 | Node-wise | 33.00 | 95.00 | 180.13 | 43.21 | 278.05 | 6.61 | 1.91 |
| SIFT | 16 | ER | 33.00 | 95.00 | 13.37 | 42.07 | 110.15 | 2.08 | 0.98 |
| SIFT | 32 | Record-wise | 32.00 | 95.00 | 242.34 | 55.70 | 356.57 | 10.42 | 2.99 |
| SIFT | 32 | Node-wise | 32.00 | 95.00 | 224.44 | 56.61 | 339.58 | 8.71 | 2.66 |
| SIFT | 32 | ER | 32.00 | 95.00 | 12.90 | 55.42 | 126.85 | 2.45 | 1.41 |
| SIFT | 64 | Record-wise | 31.00 | 95.50 | 256.01 | 59.82 | 374.04 | 10.88 | 2.99 |
| SIFT | 64 | Node-wise | 31.00 | 95.50 | 238.27 | 62.27 | 358.76 | 9.02 | 2.78 |
| SIFT | 64 | ER | 31.00 | 95.50 | 12.70 | 59.54 | 130.46 | 2.42 | 1.36 |
| DEEP | 8 | Record-wise | 94.00 | 95.00 | 385.13 | 61.87 | 602.65 | 17.91 | 4.77 |
| DEEP | 8 | Node-wise | 94.00 | 95.00 | 335.55 | 62.63 | 553.83 | 14.74 | 4.17 |
| DEEP | 8 | ER | 94.00 | 95.00 | 38.14 | 61.02 | 254.81 | 5.97 | 2.43 |
| DEEP | 16 | Record-wise | 49.00 | 95.00 | 336.28 | 57.66 | 546.76 | 15.51 | 4.27 |
| DEEP | 16 | Node-wise | 49.00 | 95.00 | 307.82 | 58.41 | 519.06 | 13.07 | 3.83 |
| DEEP | 16 | ER | 49.00 | 95.00 | 21.35 | 57.23 | 231.40 | 5.17 | 2.29 |
| DEEP | 32 | Record-wise | 40.00 | 95.00 | 358.17 | 64.81 | 584.95 | 17.67 | 4.85 |
| DEEP | 32 | Node-wise | 40.00 | 95.00 | 334.08 | 65.92 | 561.98 | 14.92 | 4.42 |
| DEEP | 32 | ER | 40.00 | 95.00 | 17.65 | 64.46 | 244.08 | 5.65 | 2.45 |
| DEEP | 64 | Record-wise | 37.00 | 95.50 | 377.98 | 70.22 | 601.73 | 18.46 | 5.26 |
| DEEP | 64 | Node-wise | 37.00 | 95.50 | 355.43 | 72.11 | 581.07 | 15.65 | 4.82 |
| DEEP | 64 | ER | 37.00 | 95.50 | 16.39 | 69.90 | 239.82 | 5.53 | 2.71 |

## ER 相对其他认证组织的降幅

负数表示退化，保留原值，不做单调修正。各个 M 重新校准 E，因此不能仅依据 M 推断指标必须单调。

- SIFT, M=8, ER 相对 Record-wise：routing proof / Total VO / V / Q 降幅分别为 89.71% / 63.33% / 74.80% / 52.57%。
- SIFT, M=8, ER 相对 Node-wise：routing proof / Total VO / V / Q 降幅分别为 88.08% / 59.50% / 67.84% / 45.38%。
- SIFT, M=16, ER 相对 Record-wise：routing proof / Total VO / V / Q 降幅分别为 93.26% / 62.71% / 74.08% / 54.75%。
- SIFT, M=16, ER 相对 Node-wise：routing proof / Total VO / V / Q 降幅分别为 92.58% / 60.39% / 68.58% / 48.72%。
- SIFT, M=32, ER 相对 Record-wise：routing proof / Total VO / V / Q 降幅分别为 94.68% / 64.43% / 76.54% / 52.84%。
- SIFT, M=32, ER 相对 Node-wise：routing proof / Total VO / V / Q 降幅分别为 94.25% / 62.64% / 71.94% / 47.14%。
- SIFT, M=64, ER 相对 Record-wise：routing proof / Total VO / V / Q 降幅分别为 95.04% / 65.12% / 77.75% / 54.57%。
- SIFT, M=64, ER 相对 Node-wise：routing proof / Total VO / V / Q 降幅分别为 94.67% / 63.64% / 73.16% / 51.06%。
- DEEP, M=8, ER 相对 Record-wise：routing proof / Total VO / V / Q 降幅分别为 90.10% / 57.72% / 66.67% / 49.08%。
- DEEP, M=8, ER 相对 Node-wise：routing proof / Total VO / V / Q 降幅分别为 88.63% / 53.99% / 59.49% / 41.71%。
- DEEP, M=16, ER 相对 Record-wise：routing proof / Total VO / V / Q 降幅分别为 93.65% / 57.68% / 66.69% / 46.28%。
- DEEP, M=16, ER 相对 Node-wise：routing proof / Total VO / V / Q 降幅分别为 93.06% / 55.42% / 60.47% / 40.25%。
- DEEP, M=32, ER 相对 Record-wise：routing proof / Total VO / V / Q 降幅分别为 95.07% / 58.27% / 68.01% / 49.47%。
- DEEP, M=32, ER 相对 Node-wise：routing proof / Total VO / V / Q 降幅分别为 94.72% / 56.57% / 62.11% / 44.57%。
- DEEP, M=64, ER 相对 Record-wise：routing proof / Total VO / V / Q 降幅分别为 95.66% / 60.14% / 70.04% / 48.48%。
- DEEP, M=64, ER 相对 Node-wise：routing proof / Total VO / V / Q 降幅分别为 95.39% / 58.73% / 64.65% / 43.72%。

Record-wise 明确采用每条有向边作为独立 record；Node-wise 为整个节点的多层邻接表，而不是每层一个邻接表。结论针对 README 中定义的具体实现与 Merkle 组织，不能外推到所有可能的 record 划分或叶子布局。客户端验证包含认证、SQ4 遍历复现、渐进边界检查和精确 top-k 认证；查询时间还受到各组织序列化实现的影响。

校准和测量共享 20 条查询，重复计时不增加独立召回率样本量。不同 M 的实际 Recall 可能略有差异；工作簿记录实际数值而不是统一填95%。原有实验与 work9.xlsx 保持只读。
