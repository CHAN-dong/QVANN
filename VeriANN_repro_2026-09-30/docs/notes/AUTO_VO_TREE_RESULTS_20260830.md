# AutoVO-Tree 自适应可验证 ANN 实验

日期：2026-08-30

## 结论

简单的结构搜索方案是可用的：它在 SIFT 的三个规模上自动选择两层，在具有明显层次几何的第二组数据上自动选择三层；四组正式测试的 Recall@100 均超过 0.99，验证违规均为 0。SIFT100K 上相对同构建预算的手调两层最优点，自动方案的规范 Huffman 总 VO 高 4.06%，但 ANN 查询延迟低 26.89%。

## 方法

AutoVO-Tree 不使用历史查询频率或在线 workload 日志来设计树。它使用 base vectors 构建候选树，只用一个独立的小 calibration query 集保证统计 Recall 约束。

1. 叶簇数只考虑不超过 `min(N, max_fine)` 的最大 2 次幂及其一半。
2. 对每个叶簇数构造 balanced residual hierarchy，比较 1、2、3 层。
3. 两层分支候选为 32/64/128；三层等分支候选为 8/16/32。
4. 每个结构只在一个很小的固定比例网格中搜索各层 probe，选择满足 calibration Recall 的最小 VO 点，再在所有结构间取全局最小。

选择目标为：

```
C_route = parameters + codec models/codes + metadata + 32 * MerkleSiblingCount
C_leaf_proxy = visited_objects * (d/2 + 32 + 4 + 8)
minimize min(C_fixed, C_Huffman) + C_leaf_proxy
subject to Recall_cal >= Recall_target + margin
```

`C_route` 使用实际认证布局的字节公式；`C_leaf_proxy` 是保守结构代理。选中的配置随后用完整 progressive leaf VO 协议重跑，不以代理值充当最终结果。

重要边界：VO 目标是结构性的、与 workload 频率无关；Recall 是分布统计量，因此仍需独立 calibration queries。若完全不允许任何查询样本，就只能使用通常过松、实用性很差的 worst-case Recall 保证。

## 实验协议

- SIFT：128 维，base 取 50K/100K/200K；`sift_learn` 前 60 条仅用于选择；正式结果使用 `sift_query` 1000 条。
- HierGauss64：64 维层次高斯混合，50K base、5K learn、1K query，独立生成并取整到 `[0,255]`。
- 目标：Recall@100 >= 0.99；选择阈值为 0.991（0.001 safety margin）。
- 建树：4 次 balanced k-means 迭代；PQ 训练最多 12K 个中心、4 次迭代。
- 路由编码：root SQ4；两层 residual PQ24；三层 middle PQ24、fine PQ16。
- 最终 VO：路由参数、编码模型、中心码、选择元数据、Merkle proofs、叶对象渐进证明全部计入；固定宽度与规范 Huffman 取较小者。

## 正式结果

| 数据 | 自动结构 | 自动 probes | Recall@100 | Routing VO | Leaf VO | 最终总 VO | ANN 查询 | VO 生成 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| SIFT50K | 512 -> 32768（2 层，B=64） | 102 -> 721 | 0.99105 | 269.36 KiB | 126.49 KiB | 395.85 KiB | 2.291 ms | 1.605 ms |
| SIFT100K | 512 -> 65536（2 层，B=128） | 102 -> 786 | 0.99035 | 429.74 KiB | 139.54 KiB | 569.28 KiB | 2.695 ms | 1.636 ms |
| SIFT200K | 512 -> 65536（2 层，B=128） | 133 -> 1442 | 0.99047 | 543.40 KiB | 430.66 KiB | 974.06 KiB | 4.384 ms | 5.926 ms |
| HierGauss64-50K | 64 -> 1024 -> 16384（3 层，B=16/16） | 5 -> 32 -> 100 | 0.99127 | 45.05 KiB | 15.92 KiB | 60.98 KiB | 0.171 ms | 0.245 ms |

四个配置的 `violations=0`。SIFT 使用 Huffman 后更小；HierGauss64 的 Huffman 模型头部使其总 VO 为 62.13 KiB，高于固定宽度的 60.98 KiB，因此修订后的选择器会自动选择固定宽度。

## 深度确实由数据决定

| 数据与深度 | 满足阈值的最小 calibration proxy VO | 结构/probes |
|---|---:|---|
| SIFT100K，1 层 | 1543.09 KiB | K=65536，probe=1769 |
| SIFT100K，2 层 | 566.46 KiB | 512 -> 65536，102 -> 786 |
| SIFT100K，3 层 | 585.02 KiB | 256 -> 4096 -> 65536，102 -> 653 -> 1254 |
| HierGauss64，1 层 | 297.40 KiB | K=16384，probe=100 |
| HierGauss64，2 层 | 80.55 KiB | 128 -> 16384，10 -> 115 |
| HierGauss64，3 层 | 59.69 KiB | 64 -> 1024 -> 16384，5 -> 32 -> 100 |

因此“两层通常够用”不是硬编码结论：SIFT100K 在第三层增加的路由参数、模型和中间证明超过其叶扫描收益，算法停在两层；层次混合数据的第三层进一步减少不可能的叶对象，节省 25.9% 的 calibration proxy VO，算法继续到三层。

## 与手调两层的公平比较

| SIFT100K | 结构/probes | Recall@100 | 固定总 VO | Huffman 总 VO | ANN 查询 | VO 生成 |
|---|---|---:|---:|---:|---:|---:|
| 手调 balanced 2L | 1024 -> 65536，173 -> 768 | 0.99021 | 566.64 KiB | 547.08 KiB | 3.686 ms | 1.766 ms |
| AutoVO-Tree | 512 -> 65536，102 -> 786 | 0.99035 | 582.60 KiB | 569.28 KiB | 2.695 ms | 1.636 ms |

自动方案相对手调：Recall +0.00014；固定 VO +2.82%；最佳编码 VO +4.06%；ANN 延迟 -26.89%；VO 生成 -7.38%。这个差距符合“无需手调且不离最优太远”的目标。

## 代理模型准确度

| 数据 | 选择阶段 proxy | 完整协议 VO | proxy 相对误差 |
|---|---:|---:|---:|
| SIFT50K | 386.06 KiB | 395.85 KiB | -2.47% |
| SIFT100K | 559.12 KiB | 569.28 KiB | -1.79% |
| SIFT200K | 1097.51 KiB | 974.06 KiB | +12.67% |
| HierGauss64-50K | 60.11 KiB | 60.98 KiB | -1.42% |

路由模型很准确；200K 的偏差主要来自简单 leaf proxy 对高叶密度时渐进剪枝收益估计不足。因此代理适合便宜地排序候选，但最终候选仍应运行一次完整 VO evaluator。

## 适用性与限制

- 适合路由 VO 与叶遍历存在明显权衡、且中心可用 balanced residual hierarchy 表示的数据。
- 随 N 从 50K 增到 200K，算法自动提高 probes；200K 被 `max_fine=65536` 限制，叶 VO 明显增长，下一步应允许更大叶簇数或继续加层。
- 不依赖热门查询或访问频率；calibration set 只承担 Recall 约束。现有完整协议仍用 learn queries 优化叶证明位宽和 Merkle 排列，这与树结构选择正交，严格端到端 workload-free 版本可固定 identity layout。
- 当前 probes 是数据集级静态值，不是逐查询动态值；后者需要额外的风险校准机制，否则容易在难查询上破坏 Recall。
- 第二种几何目前是合成数据，只证明算法能因数据结构而选择三层，不能替代 GIST/Deep/Glove 等标准数据集的外部有效性实验。
- 原型搜索支持 1--3 层并自动选择，尚不是任意深度递归器。论文版应采用“新增一层后的最优 VO 改善小于 epsilon 即停止”的生成式搜索，并加入 depth 4 作为未触及上限的检查。

## 可复现文件

- 选择器：`auto_vo_tree.cpp`，构建目标 `auto_vo_tree_experiment`。
- 完整三层 VO：`residual_3level_vo.cpp`，已支持 balanced groups、selector seeds 和 Huffman 字节核算。
- 第二数据几何：`generate_hiergauss.py`。
- 选择 CSV：`auto_vo_sift{50k,100k_v2,200k}.csv`、`auto_vo_hiergauss50k_v2.csv`。
- 完整 CSV：`auto_selected_full_*.csv`；公平手调基线：`manual_balanced_2l_matched_c173_p768_q1000.csv`。

示例：

```
make auto_vo_tree_experiment
./auto_vo_tree_experiment --dataset ../dataset/sift --base 100000 --max-fine 65536 --cal-queries 60 --test-queries 200 --target 0.99 --margin 0.001 --output auto_vo_sift100k_v2.csv
```

## 总体判断

这个版本已经证明核心 idea 可行：一个很小的、可解释 VO cost model 能同时决定叶簇数、分层数、分支数和每层 top-probe，并在 SIFT100K 上把免手调代价控制在手工最优的约 4% 内。它可以作为论文方法原型，但在声称“通用自适应树构建”前，还需要任意深度停止规则、至少 3 个标准数据集、多个随机种子，以及 build time/memory 报告。
