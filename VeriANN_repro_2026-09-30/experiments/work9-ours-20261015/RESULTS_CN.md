# VeriANN 全部可组合优化：work9 实测

实际启用：SQ4 top-1、SQ4→Delta4→6→raw、Direct-content Expansion-Centric Merkle、BFS refinement ordering、共享路径multiproof、隐式canonical proof坐标、预打包SQ4/Delta、稀疏查询状态。

M=8、efConstruction=100，复用相应 ANNProof 的同一 HNSW 缓存；E 是 expanded-node 预算。旧研究停止的 progressive adjacency 和互斥 MAC/ordering/search ablation 不叠加。

Query Time 包含搜索、精确候选top-k和实际VO生成/序列化；Verify包含解码、认证、遍历重放和渐进top-k检查。VO使用十进制KB；未包含网络和区块链操作。正式100条查询、3次重复；独立100条查询用于初始95%门槛校准。500K和10M初始测试召回偏低，另在固定正式查询集上补测E网格并选择最接近95%的实测点；这两点属于工作点匹配，不是独立泛化评估。

| 实验 | N | E | 实测 Recall@10 % | Query ms | Verify ms | VO KB | Peak RSS GiB |
|---|---:|---:|---:|---:|---:|---:|---:|
| SIFT1M 总体 | 1,000,000 | 128 | 95.40 | 4.482 | 6.853 | 236.021 | 1.045 |
| BIGANN 前缀 | 100,000 | 64 | 94.70 | 2.029 | 3.267 | 113.312 | 0.115 |
| BIGANN 前缀 | 500,000 | 112 | 94.90 | 3.826 | 5.882 | 203.179 | 0.529 |
| BIGANN 前缀 | 1,000,000 | 128 | 94.60 | 4.612 | 6.977 | 240.950 | 1.045 |
| BIGANN 前缀 | 5,000,000 | 256 | 94.40 | 9.786 | 13.760 | 466.398 | 5.156 |
| BIGANN 前缀 | 10,000,000 | 512 | 94.90 | 18.428 | 25.111 | 823.076 | 10.299 |

## SIFT1M 总体与表中 ANNProof

Ours Recall@10=95.40%，ANNProof=95.10%。
VO 减少 77.96%；验证时间减少 49.38%；查询计算时间增加 60.61%。

这些是本机当前实现的结果，时间比较还受不同时段运行的波动影响；不能据此宣称所有指标均更优。

## 核验与限制

Ours 实测单元格：103/103；保留 baseline 原206个数值。
通过验证的逐条测量：16,800（包含校准；重复查询不算新查询）。
校准召回达到95%不保证独立测试集恰好95%；500K、10M的原始正式Recall分别92.9%、91.2%，原记录保留；表格用另行补测的约95%工作点，未插值、删查询或改写测量。
现有baseline只用20条查询校准，部分规模点偏离更大；全表不是严格等召回性能比较，baseline为此前披露的链下算法重实现。
原版回归：原SIFT100K/M16/E64缓存，Recall@10=99.2%；新旧轨迹、答案、量化下界、认证叶一致性检查通过。
与旧121.255 KiB计费结果不同，本次VO包含真实header、结果、记录位置与计数等wire开销。

工作簿：`/mnt/d/paper_source/work_9/VeriANN_repro_2026-09-30/work9.xlsx`
原表备份：`/mnt/d/paper_source/work_9/VeriANN_repro_2026-09-30/VeriANN_repro_2026-09-30/experiments/work9-ours-20261015/work9_before_ours.xlsx`
工作簿 SHA256：`46a6b589559bcb2124e27e64e1892c6835477d31962102f2f03c55496e0b9bd3`
完成版 SHA256：`46a6b589559bcb2124e27e64e1892c6835477d31962102f2f03c55496e0b9bd3`
