# 单参数 efVO 渐进式 residual 查询实验

日期：2026-09-01  
目标：在 AutoVO 自适应 residual 层次索引上，用一个用户参数 `efVO` 替代每层 `top-p`，同时维持低完整 VO、可接受的查询/构建开销和不低于固定 probe 基线的 Recall@100。

## 1. 结论

**这个方法在本轮实验范围内 work，但应区分“VO/查询机制可用”与“无需校准即可准确预测 Recall”两件事。**

- 在四个 recall-matched 工作点上，Recall 都不低于对应的固定 probe 基线。
- 完整 VO 相对固定 probe 最优点为 `+6.1% / +1.6% / +5.9% / -6.0%`。
- SIFT20K、SIFT50K 查询更快；SIFT100K 查询只慢 7.0%；HierGauss 查询增加 0.066 ms。
- 查询与 VO 生成合计时间变化为 `-13.1% / -10.7% / +12.6% / +17.5%`。HierGauss 的相对数较大，但绝对增加仅 0.075 ms。
- 索引构建为 0.90--6.14 s，相对原 AutoVO 结构约增加 6%--13%；几何参数的小网格评估可以复用同一棵索引，不需要重复聚类。
- 所有正式曲线均为 `prefix_violations=0`、`object_violations=0`；非最低安全点也没有强制越过预算。

所以，对论文的核心目标可以表述为：**单参数、认证可重放的渐进式 residual traversal 能在不同规模和不同几何数据上接近固定 probe 的 VO--Recall Pareto 前沿，无需在查询中携带逐层 probe 映射表。**

不应表述为：任意新数据集上，仅凭一个 `efVO` 数值就能无校准地严格保证某个统计 Recall。后者仍需要独立 calibration 或更保守的 guard。

## 2. 最终协议

### 2.1 索引

树的层数、各层节点数和 residual codec 继续由 AutoVO 构建器选择。本实验直接使用上一轮自动选出的四棵树：

| 数据 | residual hierarchy | codec |
|---|---|---|
| SIFT20K | 512 → 16,384 | SQ4 → PQ24 |
| SIFT50K | 512 → 32,768 | SQ4 → PQ24 |
| SIFT100K | 256 → 4,096 → 65,536 | SQ4 → PQ24 → PQ16 |
| HierGauss64-50K | 64 → 1,024 → 16,384 | SQ4 → PQ24 → PQ16 |

父节点保存量化中心，子节点保存相对父节点的量化 residual。每个 child block、节点 metadata 和叶对象摘要都由同一棵认证索引树承诺；不是每层另建一棵互不相关的 Merkle tree。

### 2.2 单参数查询

服务端维护一个包含所有当前可展开节点的全局优先队列。节点动作分数为：

```text
score(u) = geometric_score(q, u)
           + lambda * log(1 + estimated_action_bytes(u) / 1024)
```

本轮正式结果使用 `lambda=0`。`geometric_score` 是量化 residual 距离，并用索引构建时得到的典型覆盖半径做轻量 optimistic correction。这个修正只用于让不同深度的分数可比较；模式和系数是索引元数据，不是用户参数。

每次弹出全局最优动作：

1. 内部节点：认证其 metadata/child residual block，将孩子加入队列；
2. 叶节点：访问该叶对象并加入候选集；
3. 若下一个动作会使认证 effort charge 超过 `efVO`，且已有至少 `k` 个候选对象，则结束；
4. 对候选对象做精确距离排序，得到 top-k。

因此查询没有 `p_c / p_m / p_f`，也没有 `efVO → probe vector` 查表。增加 `efVO` 只延长同一条确定性动作 transcript；实验中较大预算的 transcript 始终包含较小预算 transcript 的完整前缀。

### 2.3 VO 与验证

搜索和 VO 序列化在实现上分为两个阶段，但 VO 由搜索 transcript 唯一决定：

- routing VO：量化参数、实际打开的 residual blocks、节点 metadata、动作序列和 Merkle multiproof；
- leaf VO：渐进量化摘要、必要的精确对象、叶内 Merkle multiproof；
- verifier：重建优先队列，逐步检查每次弹出的确是当时最优动作，重算 action charge 和停止条件，再验证 top-k 与对象排除证书。

验证者只需索引承诺中的少量几何参数和用户给出的 `efVO`，不需要接收或认证一张 Recall/probe 映射表。

## 3. 实验设置

- `k=100`，每个最终结果使用 500 条 test queries；exact top-100 作为 Recall ground truth。
- 120 条独立 learn queries 用于 leaf certificate 训练和内部几何参数选择；最终 test set 不参与保守配置选择。
- 完整 VO 包括参数、code/model、metadata、framing、Merkle proofs、渐进摘要与精确对象；KiB 按 1024 bytes。
- C++17，`-O3 -DNDEBUG`，单进程顺序运行；延迟为本机一次正式运行均值，不是跨机器基准。
- 固定 probe 基线来自同一索引、同一完整 VO 账本和同一 500-query test set。

## 4. Recall-matched 结果

这里比较实际 test Recall 不低于固定 probe 的最小已测 `efVO` 点。它回答核心问题：在相同实际检索质量下，单参数协议是否破坏 VO 和查询效率。

| 数据 | efVO | Recall@100 | Routing VO | Leaf VO | 完整 VO | ANN 查询 | VO 生成 | 在线合计 | 构建 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SIFT20K | 340 | 0.99398 | 200.42 KiB | 72.77 KiB | **273.19 KiB** | 1.386 ms | 0.854 ms | 2.239 ms | 1.11 s |
| SIFT50K | 525 | 0.99108 | 269.27 KiB | 134.97 KiB | **404.23 KiB** | 1.744 ms | 1.481 ms | 3.225 ms | 2.80 s |
| SIFT100K | 850 | 0.99250 | 369.98 KiB | 240.16 KiB | **610.14 KiB** | 4.154 ms | 2.784 ms | 6.939 ms | 6.14 s |
| HierGauss64-50K | 65 | 0.99360 | 46.25 KiB | 11.06 KiB | **57.31 KiB** | 0.241 ms | 0.262 ms | 0.503 ms | 0.90 s |

与固定 probe AutoVO 比较：

| 数据 | 固定 probe Recall / VO | efVO Recall / VO | VO 变化 | ANN 查询变化 | 在线合计变化 |
|---|---|---|---:|---:|---:|
| SIFT20K | 0.99210 / 257.48 KiB | 0.99398 / 273.19 KiB | +6.1% | -28.2% | -13.1% |
| SIFT50K | 0.99044 / 397.76 KiB | 0.99108 / 404.23 KiB | +1.6% | -18.0% | -10.7% |
| SIFT100K | 0.99214 / 576.18 KiB | 0.99250 / 610.14 KiB | +5.9% | +7.0% | +12.6% |
| HierGauss64-50K | 0.99168 / 60.99 KiB | 0.99360 / 57.31 KiB | -6.0% | +37.5%（+0.066 ms） | +17.5%（+0.075 ms） |

这组结果支持“主要优化 VO，同时只允许不大的查询/构建 tradeoff”的目标。尤其是 100K 三层树没有因为全局调度而出现 VO 爆炸。

## 5. 参数是否真的可调

正式预算曲线的代表点如下：

| 数据 | efVO 范围 | Recall 范围 | 完整 VO 范围 | ANN 查询范围 |
|---|---:|---:|---:|---:|
| SIFT20K | 280 → 380 | 0.98036 → 0.99414 | 231.13 → 294.31 KiB | 1.00 → 1.28 ms |
| SIFT50K | 475 → 650 | 0.98704 → 0.99600 | 373.02 → 470.72 KiB | 1.61 → 2.09 ms |
| SIFT100K | 650 → 1,000 | 0.97918 → 0.99574 | 488.19 → 695.77 KiB | 3.75--4.95 ms |
| HierGauss64-50K | 55 → 85 | 0.98290 → 0.99482 | 50.41 → 69.25 KiB | 0.16 → 0.36 ms |

Recall 与 VO 随预算总体单调上升，所有相邻预算的 transcript 前缀检查均为 0 违规。因此 `efVO` 确实提供了类似 IVF `nprobe` / HNSW `efSearch` 的单调 tradeoff，而不是一个只对应单点的装饰参数。

## 6. 自我迭代中发现和修正的问题

1. **逐层固定 top-p**：效果好，但需要用户理解多个参数，也会引出映射表认证问题；被全局预算替代。
2. **逐节点动态内存分配**：SIFT100K 延迟偏高；改为 lazy residual 解码和连续解码池后，100K 查询回到约 4 ms。
3. **用每个节点 RMS 半径直接修正所有数据**：在强层次数据上会偏爱宽簇；加入 level-constant 与 per-node 两种构建期候选，由独立 calibration 选择。
4. **HierGauss 中层误用 PQ16**：导致 Recall 长时间停在约 0.988；恢复 AutoVO 选择的 PQ24 后问题消失。错误结果未进入最终表。
5. **层级 fairness/probe guard**：曾尝试存一组参考 probe 比例防止 frontier starvation；修正 codec 后证明不需要，最终实现已移除，避免重新引入逐层参数。
6. **共享指针热路径**：改为整数 slot + 连续 decoded pool；访问序列、Recall 和 VO 保持不变，小查询延迟下降。

## 7. 保守 calibration guard 的代价

若要求仅根据 120 条 learn queries、在看 test 结果之前选择一个带更大 Recall margin 的点：

- SIFT100K 需从 `efVO=850` 提到 `900`：Recall 0.99392，VO 638.998 KiB（相对固定 probe +10.9%），ANN 查询 4.402 ms（+13.4%）。
- HierGauss 需从 VO-optimized 的 `efVO=65` 提到 `85`：Recall 0.99482，VO 69.25 KiB（+13.5%），ANN 查询 0.355 ms。绝对增加仍小，但在线合计相对基线约增加 46%。

这说明当前薄弱点不是认证查询或 VO，而是小 calibration set 上的 Recall margin 估计。按照本项目已经确定的重点，建议把它作为工程扩展，不把“精确预测用户 Recall”包装成核心贡献。

## 8. 仍需明确的限制

- `efVO` 当前是 verifier 可重算的 **effort byte proxy**，不是最终 multiproof 压缩后 VO 的严格上限。正式 SIFT 点上 charge 比实际完整 VO 高约 24%--39%，但很小的索引或低预算下也可能因固定参数/leaf framing 而低估实际 VO。下一步可用在线 Merkle multiproof 边际字节和完整固定头部替代逐动作路径估计，使参数刻度既更紧又成为真正的上界。
- Recall 是统计性质，认证协议可以证明服务器遵守了 `efVO` 和搜索算法，但不能从密码学上证明数据分布上的 Recall。
- 几何修正模式和 `alpha` 目前从一个很小的构建期候选集合选择；用户不可见，但严格的完全 workload-free 版本仍应改成 held-out base pseudoqueries 或纯几何规则。
- 当前只有 SIFT 的 20K/50K/100K 和一个 64 维层次高斯数据；论文最终还应补至少一个真实的非 SIFT 数据集和更大规模。
- 延迟为研究原型单次本机均值，尚未做多轮置信区间、冷/热缓存分离和并发吞吐实验。

## 9. 复现文件

- 实现：`budgeted_residual_vo.cpp`
- 构建：`make budgeted_residual_vo_experiment`
- calibration-selected CSV：
  - `efvo_final_sift20k.csv`
  - `efvo_final_sift50k.csv`
  - `efvo_final_sift100k.csv`
  - `efvo_final_hiergauss.csv`
  - `efvo_final_hiergauss_safe.csv`
- 完整曲线：
  - `efvo_formal_sift20k.csv`、`efvo_formal_sift20k_360.csv`
  - `efvo_formal_sift50k.csv`
  - `efvo_formal_sift100k.csv`
  - `efvo_formal_hiergauss.csv`、`efvo_formal_hiergauss_low.csv`

## 10. 最终判断

**可用，且值得继续作为论文主查询协议。** 单参数 best-first residual traversal 已经证明可以保住完整 VO，并在 20K--100K 与不同数据几何上维持目标 Recall 和毫秒级查询。它比“每层 top-p + 映射表”更容易解释和验证。

但论文中应把结论限定为“接近固定 probe Pareto 前沿的认证单参数查询”，而不是“无校准 Recall 保证”。下一轮最有价值的工作是：收紧 `efVO` charge 与实际 multiproof VO 的差距，并补充真实跨数据集与多轮延迟统计。
