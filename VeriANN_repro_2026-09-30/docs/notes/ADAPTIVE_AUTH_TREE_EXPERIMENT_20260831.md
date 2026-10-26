# Adaptive Authenticated Tree 实验报告

日期：2026-08-31

## 1. 结论

本轮实现并验证了完整链路：物理字节上限产生 atomic micro-clusters；局部近邻
候选自底向上构造 dendrogram；base-data pseudo-queries 驱动认证门
keep/collapse；查询使用量化误差感知安全下界、全局 best-first frontier 和
min(frontier LB) >= gamma * tau_k 停止规则；独立 calibration queries 将用户
Recall 目标映射到 gamma；最后建立并重放递归 SHA-256 commitment。

实验结论不是“全面通过”：

- **构建成本：通过。** SIFT100K 完整构建约 2.61 s，峰值内存约 84 MiB。
- **正确性：通过。** 所有正式实验半径违规、transcript 违规均为 0；
  gamma=1 的 Recall 均为 1。
- **0.99 Recall 控制：通过。** 正式配置测试误差为 +0.00048 到 +0.00324。
- **认证门压缩：有效但有限。** 相对完整 dendrogram，VO 降低 1.5%--5.1%，
  ANN 查询时间降低 2.3%--17.0%。
- **通用低 VO：未通过。** SIFT50K/100K 的安全树 VO 分别是现有 AutoVO
  residual hierarchy 的 4.33 倍和 5.27 倍。

安全 best-first 树适合作为 exact/high-assurance fallback，不能替代 SIFT
这类高维重叠数据上的 balanced residual 2L/3L 主路径。

## 2. 实现

- 源码：adaptive_auth_tree.cpp
- 构建目标：make adaptive_auth_tree_experiment
- 每次运行输出正式结果 CSV 和 gamma/Recall/VO calibration CSV。

### 2.1 Atomic micro-clusters

对象认证摘要记录大小为 ceil(d/2) + object_id(4 B) + quantization_error(4 B)。
由 atomic byte cap 计算对象容量，并向上取 2 的幂得到 micro-cluster 数量。
micro-cluster 使用 capacity-balanced hierarchical k-means 构造。

### 2.2 自底向上 dendrogram

每轮在当前簇中心方差最大的若干维上排序，在局部窗口内产生近邻合并候选。
合并分数计入合并后覆盖半径、对象数不平衡和认证 descriptor 相对被隐藏对象
摘要的字节比例。它避免了全对全 agglomerative clustering 的二次复杂度。

### 2.3 Gate-collapse

第一版曾把 collapse 实现成“大扫描叶”，导致叶 VO 上升。最终实现采用正确语义：
删除无收益认证门，并把其保留后代提升到最近的保留祖先。micro-leaf 不变，
保留节点可以拥有可变数量的有效孩子，最大 child block fanout 默认为 64。

cost model 只使用 base vectors 产生的 pseudo-queries，不读取业务访问日志：

    collapse = parent_visits * exposed_descendant_descriptors
    keep     = parent_visits * gate_descriptor
             + gate_survivals * child_block_descriptors

### 2.4 查询和认证

节点安全下界为：

    LB(q,u) = max(0, ||q - decoded_center_u|| - qerr_u - radius_u)

服务器和验证者使用确定性的 (LB, center_distance, node_id) 次序维护 frontier。
VO builder 对查询期间保存的 trace 去重并计算认证字节，不在普通查询后猜测路径。
全局根递归认证每个有效 child block；没有为每个 level 单独建立 Merkle Tree。

## 3. 实验协议

- k=100
- 路由摘要：PQ16，descriptor 68 B
- 叶对象摘要：SQ4 安全下界
- calibration：300 条独立 learn queries
- test：500 条独立 official queries
- Recall 选择：单侧 99% query-level LCB
- safety margin：0.002
- atomic 候选：2/4/8 KiB；正式结果使用满足 Recall 后 VO 最小的 4 KiB
- commitment：实际 SHA-256

VO 计入根和量化参数、child descriptors、叶摘要、精确对象 payload、Merkle
multiproof 和 framing。query_ms 是服务器 ANN 遍历；vo_generation_ms 是开启
集合和 VO 字节生成；verify_ms 包含下界重放及 SHA-256。计时不含网络、签名验证
和真实缓冲区序列化。

## 4. 正式结果：Recall 目标 0.99

| 数据 | micro | full internal -> retained | avg depth | gamma | calibration LCB99 | test Recall | error | VO | query | VO gen | verify | build |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SIFT20K | 512 | 511 -> 11 | 2.05 | 0.040 | 0.99226 | 0.99048 | +0.00048 | 841.07 KiB | 1.33 ms | 1.47 ms | 1.62 ms | 0.40 s |
| SIFT50K | 1,024 | 1,023 -> 22 | 2.02 | 0.001 | 0.99520 | 0.99324 | +0.00324 | 1,712.11 KiB | 4.21 ms | 4.21 ms | 2.79 ms | 1.34 s |
| SIFT100K | 2,048 | 2,047 -> 52 | 2.27 | 0.007 | 0.99204 | 0.99120 | +0.00120 | 3,002.30 KiB | 8.50 ms | 7.80 ms | 4.61 ms | 2.61 s |
| HierGauss64-50K | 512 | 511 -> 99 | 2.45 | 0.001 | 1.00000 | 1.00000 | +0.01000 | 136.27 KiB | 0.21 ms | 0.23 ms | 0.33 ms | 0.60 s |

所有行的 radius_violations=0、transcript_violations=0、
exact_mode_recall=1。

## 5. Collapse 是否有效

| 数据 | VO reduction vs full dendrogram | query reduction | verify reduction |
|---|---:|---:|---:|
| SIFT20K | 1.50% | 16.95% | 8.98% |
| SIFT50K | 1.73% | 13.06% | 14.98% |
| SIFT100K | 1.80% | 15.93% | 17.49% |
| HierGauss64-50K | 5.05% | 2.33% | 10.94% |

VO 改善小于查询改善，因为 SIFT 总 VO 由叶对象摘要主导。SIFT100K 平均仍扫描
约 35,008 个对象；认证门压缩只能减少路由 descriptor，不能修复高维球下界过松。

## 6. Atomic 粒度实验

SIFT20K，目标 Recall 0.99，300 calibration / 500 test：

| atomic cap | micro | gamma | test Recall | VO | query | build |
|---:|---:|---:|---:|---:|---:|---:|
| 2 KiB | 1,024 | 0.080 | 0.99036 | 863.21 KiB | 1.54 ms | 0.47 s |
| 4 KiB | 512 | 0.040 | 0.99048 | **841.07 KiB** | 1.33 ms | 0.40 s |
| 8 KiB | 256 | 0.001 | 0.98974 | 844.25 KiB | **1.23 ms** | 0.34 s |

过细会增加 routing descriptors，并可能要求更大的 gamma；过粗会增加对象扫描。
4 KiB 是满足 Recall 后最小 VO 点。8 KiB 仅低 0.00026，但严格约束下不应选择。

## 7. 与现有低-VO AutoVO 基线比较

| 数据 | safe adaptive tree | AutoVO residual hierarchy | VO ratio | query ratio |
|---|---:|---:|---:|---:|
| HierGauss64-50K | 136.27 KiB / 0.210 ms | 60.98 KiB / 0.171 ms | 2.23x | 1.23x |
| SIFT50K | 1,712.11 KiB / 4.214 ms | 395.85 KiB / 2.291 ms | 4.33x | 1.84x |
| SIFT100K | 3,002.30 KiB / 8.497 ms | 569.28 KiB / 2.695 ms | 5.27x | 3.15x |

两类 evaluator 都计入 stateless verifier 的路由参数、认证元数据、证明和叶对象
材料，但叶序列化细节并非完全相同。因此倍率适合做设计决策，不应声称为逐字节
同实现比较。差距仍足以排除 safe tree 作为 SIFT 主路径。

## 8. 用户 Recall 参数

高目标 Recall 控制稳定：

- SIFT20K：0.99 -> 0.99048
- SIFT50K：0.99 -> 0.99324
- SIFT100K：0.99 -> 0.99120
- HierGauss64-50K：0.99 -> 1.00000

较低目标可能明显过度满足，例如 SIFT50K 的 0.90 请求得到 0.99324。原因是
gamma=0 与最小正 gamma 之间存在大量零下界节点，Recall/VO 曲线发生跳变。
目标 Recall 是下限，因此不违反接口语义，但 safe gamma 不是所有数据集上都能
精细控制成本的连续旋钮。

## 9. 推荐架构

构建器的候选空间应同时包含 flat PQ-IVF、balanced residual 2L、balanced
residual 3L 和 gate-collapsed safe tree。统一选择规则是：

    minimize calibrated complete VO
    subject to Recall LCB99 >= requested target + margin

查询提供两个模式：

- approximate 主路径：AutoVO residual hierarchy + calibrated probes；
- exact/high-assurance fallback：本报告的安全 best-first 树，gamma=1 时
  确定性 Recall=1。

这比强迫一种树同时承担最低 VO 和确定性完备证明更符合实验结果。

## 10. 复现

    make adaptive_auth_tree_experiment

    ./adaptive_auth_tree_experiment \
      --base 100000 \
      --build-samples 64 \
      --cal-queries 300 \
      --test-queries 500 \
      --atomic-bytes 4096 \
      --iters 3 \
      --pq-train 12000 \
      --pq-iters 3 \
      --margin 0.002 \
      --targets 0.99 \
      --output formal_adaptive_auth_tree_sift100k.csv

关键结果文件：

- confirm_gate_collapse_sift20k_atomic2048.csv
- confirm_gate_collapse_sift20k_atomic4096.csv
- confirm_gate_collapse_sift20k_atomic8192.csv
- formal_adaptive_auth_tree_sift50k.csv
- formal_adaptive_auth_tree_sift100k.csv
- formal_adaptive_auth_tree_hiergauss50k.csv
- 对应的 calibration CSV

## 11. 尚未完成

- 真实 VO 二进制序列化和网络传输；
- 数字签名验证时间；
- 多随机种子；
- SIFT1M、GIST、Deep、Glove 等更多标准数据集；
- 将 2L/3L/safe-tree 候选统一到同一个可执行构建器；
- 分布漂移检测与重新校准。
