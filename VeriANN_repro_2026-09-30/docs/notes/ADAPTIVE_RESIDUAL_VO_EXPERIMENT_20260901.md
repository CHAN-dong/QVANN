# 自适应 Residual 层次可验证 ANN 实验

日期：2026-09-01

## 1. 结论

在当前可用数据和候选结构空间内，方案达到预先设定的工程目标：

- 所有正式结果的 `Recall@100 >= 0.99`，认证违规均为 0；
- SIFT100K 的最终 VO 为 576.18 KiB，距离手调两层最优 547.08 KiB 为 +5.32%，低于 15% 容忍线；
- SIFT100K ANN 查询为 3.88 ms，相对手调 3.69 ms 为 +5.28%；查询加 VO 生成的端到端时间为 +13.1%，低于 20% 容忍线；
- 所选索引的构建耗时为 0.85--5.64 s；
- 算法在 SIFT20K/50K 选择两层，在 SIFT100K 和层次高斯数据上选择三层，四层候选均因完整 VO 更高而被拒绝。

这个结论应表述为“论文原型在当前实验范围内通过”，不能表述为已经证明对任意数据集全局最优。当前只有一个标准真实数据集 SIFT 和一个合成层次数据集，且分支候选是有限集合。

## 2. 优化目标

主目标是完整 VO 最小化，而不是为每个用户请求预测 Recall：

```text
min  routing bytes + leaf/object bytes + authentication proof bytes

s.t. fixed Recall@100 constraint
     query latency budget
     index construction budget
```

本实验固定目标 Recall 为 0.99。最终采用 0.002 离线安全裕量，即结构选择要求 calibration Recall 至少为 0.992；这只是防止协议降低底层 ANN 的 Recall，不是协议的主要功能。

## 3. 自适应构建与查询搜索

### 3.1 候选结构

1. 细叶簇数取不超过 `min(N,max_fine)` 的最大 2 次幂及其一半；
2. 对 fine centers 做 balanced residual grouping；
3. 自动比较 1、2、3、4 个 routing tiers；
4. 两层 fanout 候选为 32/64/128，三层为 8/16/32，四层停止检查为 4/8；
5. 每层中心表示为父中心加局部 residual；根使用 SQ4，中间 residual 使用 PQ24，最末 residual 使用 PQ16 或 PQ24；
6. 只有增加一层能降低受约束的完整 VO 代理时才保留该层。

这不是固定“两层”算法。SIFT100K 在加入 Recall 稳定余量后，三层的额外 routing bytes 被更少的 leaf bytes 抵消，因此自动从两层改选三层。四层增加的摘要、code blocks 和 Merkle proofs 超过叶扫描收益，因此停止。

### 3.2 VO cost model

Routing 部分按真实序列化公式计费：

```text
codec parameters/models
+ root codes
+ selected-parent residual code blocks
+ selected-node metadata
+ SHA-256 Merkle multiproof siblings
```

固定宽度和规范 Huffman 两种表示取较小者。搜索阶段的叶成本使用保守代理：

```text
visited_objects * (SQ4 record + SHA sibling + id + framing)
```

选中结构后必须运行完整 progressive leaf protocol，最终结果不使用代理冒充实测值。四组最终结果中代理误差约为 -5.3%、-2.2%、+0.7%、-1.5%，足以排序候选。

### 3.3 Probe 搜索和一次失败迭代

初版在 60 条 calibration queries 上二分到刚好满足 Recall 的最小 probe。它发生明显过拟合：

- SIFT50K：calibration Recall 0.99117，test Recall 0.98796；
- SIFT20K：加入初步 guard 后，500-query test Recall 仍为 0.98940。

最终修正为：

1. 粗粒度比例网格确定稳定区域；
2. 局部二分寻找较小 probe；
3. 对细化结果增加 8% probe guard；
4. 固定 calibration 门槛从 0.991 提高到 0.992。

修正后的 SIFT20K Recall 为 0.99210，SIFT50K 为 0.99044。这个失败和修正说明，不能把小样本上“刚好过线”的 probe 当作可靠工作点。

## 4. 最终完整协议结果

每组使用 500 条独立 test queries；VO 包括 routing 参数、code/model、metadata、Merkle proofs、渐进叶摘要、精确对象和 framing。KiB 按 1024 bytes 计算。

| 数据 | 自动结构 | 自动 probes | Recall@100 | Routing VO | Leaf VO | 最佳总 VO | ANN 查询 | VO 生成 | 所选索引构建 |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| SIFT20K | 512 → 16,384（2 层） | 133 → 360 | 0.99210 | 203.86 KiB | 53.63 KiB | **257.48 KiB** | 1.930 ms | 0.648 ms | 0.99 s |
| SIFT50K | 512 → 32,768（2 层） | 102 → 721 | 0.99044 | 270.45 KiB | 127.31 KiB | **397.76 KiB** | 2.127 ms | 1.485 ms | 2.54 s |
| SIFT100K | 256 → 4,096 → 65,536（3 层） | 102 → 653 → 1,254 | 0.99214 | 374.19 KiB | 201.99 KiB | **576.18 KiB** | 3.881 ms | 2.284 ms | 5.64 s |
| HierGauss64-50K | 64 → 1,024 → 16,384（3 层） | 5 → 32 → 100 | 0.99168 | 45.06 KiB | 15.94 KiB | **60.99 KiB** | 0.175 ms | 0.253 ms | 0.85 s |

所有结果 `violations=0`。

## 5. 与基线和手调最优比较

### SIFT100K

| 方法 | Recall@100 | 最佳总 VO | ANN 查询 | VO 生成 |
|---|---:|---:|---:|---:|
| Flat PQ-IVF | 0.99131 | 861.76 KiB | 6.166 ms | 8.527 ms |
| 手调 residual 2L | 0.99021 | 547.08 KiB | 3.686 ms | 1.766 ms |
| 自适应 residual | **0.99214** | **576.18 KiB** | **3.881 ms** | **2.284 ms** |

自适应方案相对手调方案：

- Recall：+0.00193；
- VO：+5.32%；
- ANN 查询：+5.28%；
- ANN 查询加 VO 生成：5.452 ms → 6.164 ms，+13.1%。

因此达到“VO 距手调最优不超过 15%、查询端到端代价不超过约 20%、Recall 不下降”的预设要求。

### 其他对照

- SIFT50K 相对上一版 AutoVO 的 395.85 KiB，最终为 397.76 KiB，增加 0.48%；Recall 差值为 -0.00061，仍高于固定 0.99 约束；查询和 VO 生成均略快。
- HierGauss64-50K 复现上一版 60.98 KiB 的结果，差异小于 0.03%。
- 相比安全球树，SIFT20K/50K/100K 的 VO 分别从约 841/1712/3002 KiB 降到 257/398/576 KiB。主要收益来自取消过松的球下界遍历，改为认证 top-probe residual routing。

## 6. 构建成本与搜索成本

“构建一个已选定索引”和“自动搜索全部候选”必须区分：

| 数据 | 所选索引构建 | 完整 1--4 层候选合成 | 搜索峰值内存 |
|---|---:|---:|---:|
| SIFT20K | 0.99 s | 约 178 s | 44.8 MiB |
| SIFT50K | 2.54 s | 374 s | 90.8 MiB |
| SIFT100K | 5.64 s | 724 s | 160.0 MiB |
| HierGauss64-50K | 0.85 s | 106 s | 31.3 MiB |

索引构建本身是秒级，可接受；当前原型的穷举合成在 SIFT100K 上约 12 分钟，属于一次性离线成本，但仍应优化。论文实现可缓存共享 fine clustering、对劣势深度早停，并用逐层 branch-and-bound 减少候选评估。

## 7. 适用性与边界

当前可以支持的结论：

- residual hierarchy 在高维 SIFT 和明显层次化数据上都能工作；
- 自动算法能因数据规模和几何结构选择不同深度；
- 完整 VO cost model 能把自动方案控制在手调最优附近；
- 认证层没有观察到对象覆盖或 transcript 违规。

尚不能支持的结论：

- 只测试了 SIFT 一个标准真实数据集和一个合成数据集，不能声称对 Deep/GIST/Glove 等数据同样有效；
- 当前搜索深度为 1--4，fanout 使用有限候选且同层统一，不是任意深度、任意节点分支的全局最优算法；
- 当前使用独立 learn queries 估计访问对象数和约束 Recall，不使用业务访问频率或在线 workload log，但严格意义上仍是 distribution-sample-aware；完全无查询样本的版本需要改用 base-derived pseudoqueries，并单独验证；
- probes 是数据集级静态参数，不是逐查询动态 Recall 控制；这符合本工作的 VO-first 定位。

## 8. 复现文件

实现：

- `auto_vo_tree.cpp`：1--4 层结构搜索、完整 routing VO 模型、probe guard、Recall/延迟/构建预算；
- `residual_hier_vo.cpp`：完整两层 residual 查询和认证 VO；
- `residual_3level_vo.cpp`：完整三层 residual 查询和认证 VO；
- `complete_routing_vo.cpp`：公共 codec、leaf protocol 和显式 codec seed 支持。

最终结果：

- `adaptive_residual_v4_full_sift20k.csv`；
- `adaptive_residual_v3_full_sift50k.csv`；
- `adaptive_residual_v3_full_sift100k.csv`；
- `adaptive_residual_v3_full_hiergauss50k.csv`。

候选搜索和失败迭代：

- `adaptive_residual_v2_sift50k.csv`：无稳定 guard 的失败版本；
- `adaptive_residual_v3_sift50k_probe_guard.csv`：guard 修正；
- `adaptive_residual_v3_sift100k.csv`、`adaptive_residual_v3_hiergauss50k.csv`：1--4 层正式搜索；
- `adaptive_residual_v4_sift20k_margin2.csv`：小数据固定 0.002 margin 修正。

## 9. 最终判断

这个实验已经把“手动两层 residual 有效”推进为“有限候选空间内可自动选择层数、分支数和 probes，并在完整认证协议下接近手调最优”。当前结果达到原型验收标准，可以进入论文方法和消融实验阶段；在主张通用算法前，还需要至少两个标准真实数据集、多个随机种子，以及将当前有限深度搜索改写为带早停的递归生成器。
