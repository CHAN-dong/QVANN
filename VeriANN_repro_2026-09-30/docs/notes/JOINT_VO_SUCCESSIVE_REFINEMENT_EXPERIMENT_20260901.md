# VO 引导的联合逐次细化：自适应 residual 树与 probe 实验

日期：2026-09-01

## 1. 结论

方案可用，而且可以用一个核心技术点统一解决 fanout/depth 与逐层 probe 的选择：**在 log-resolution 空间做 VO-guided successive refinement**。

- 结构侧：把簇数写成 `2^j`，从细粒度叶簇开始，在现有边的 log 中点附近插入新分辨率；新增层若不能带来至少 1% 的 VO proxy 改善，就停止加深。
- 查询侧：先用一个共享比例找到 Recall 可行域，再对每层 probe 做区间二分；只做一次有界层间重分配，最后再二分去掉冗余。
- 目标：最小化完整 routing VO 加保守 leaf VO proxy，约束 calibration Recall 不低于 `target + margin`。
- 本次正式配置为 `Recall target=0.99`、`margin=0.004`、beam=3；SIFT 使用 60 条、HierGauss 使用 120 条独立 learn queries 校准，使用 500 条未见 test queries 报告最终结果。

四组正式实验的 test Recall 全部不低于 0.99，认证对象违规全部为 0。相对旧 AutoVO，完整 VO 变化为 `-1.24%` 到 `+3.70%`，查询延迟为 0.190--1.557 ms，选定索引构建为 0.82--5.50 s。主要不足是一次性联合参数搜索仍需 4.6--16.7 min。

## 2. 核心算法

### 2.1 自适应结构

1. 令 `Kmax` 为不超过数据量及配置上限的最大 2 的幂，同时比较 `Kmax/2` 与 `Kmax` 两种 micro-cluster 粒度。
2. 对每个 `Kfine=2^J` 从单层 `[J]` 开始。
3. 在一条 log-resolution 边 `[a,b]` 的中点附近插入指数 `e`，从而产生新簇数 `2^e`；第一轮检查中点附近 ±2，后续检查 ±1。
4. 每个候选结构都独立求满足 Recall 约束的 probes，并计算 VO proxy；每层保留最好的 3 条路径。
5. 若新深度的最好 proxy 相对上一深度改善小于 1%，不再向更深层展开。

因此 fanout 不是预置的候选表，而是相邻分辨率之比。例如本次自动产生了：

- SIFT20K：`256 -> 16384`，fanout 64；
- SIFT50K：`32 -> 512 -> 32768`，fanout 16/64；
- HierGauss50K：`128 -> 2048 -> 16384`，fanout 16/8。

### 2.2 自适应 probes

给定结构后：

1. 用共享比例 `alpha = 1/64, sqrt(2)/64, ... , 1` 产生一组可行初值，不需要逐层比例表。
2. 从最深层向最粗层，对 `p_l in [1, current_p_l]` 二分，保留满足 `Recall_cal >= 0.994` 且 VO 更小的点。
3. 用固定的三个分辨率步长 25%、12.5%、6.25% 尝试“缩一层、增一层”的有限重分配。
4. 再做一次逐层二分，去掉重分配后的 slack。

这仍然是普通 stagewise global top-p 查询。服务器逐层选 top-p 并扩展其子节点；客户端只验证每层认证 top-p transcript，不维护 best-first 队列，也不需要携带模型。

### 2.3 VO 目标口径

搜索阶段使用：

`VO_proxy = exact routing bytes + visited_objects * (d/2 + 44)`。

其中 routing 部分包括 codec 参数、编码中心、metadata、Merkle multiproof 和被打开的 residual blocks；leaf 部分是保守的对象证书单位成本估计。最终选定结构再运行完整量化、lossless tail、协议选择和对象覆盖检查，报告精确完整 VO。

这不是 query-frequency workload-aware 方法；learn queries 仅用于估计几何查询分布上的 Recall 与平均 VO。严格说它仍是 distribution-sample-aware，而不是完全 query-free。

## 3. 正式结果

| 数据 | 自动结构 | 自动 probes | Recall@100 | Routing | Leaf | 完整 VO | ANN 查询 | VO 生成 | 选定索引构建 | 自动搜索 |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| SIFT20K | 256 -> 16,384 | 74 -> 360 | 0.99076 | 202.79 KiB | 53.64 KiB | **256.42 KiB** | 0.777 ms | 0.649 ms | 1.09 s | 273.5 s |
| SIFT50K | 32 -> 512 -> 32,768 | 15 -> 123 -> 983 | 0.99198 | 238.43 KiB | 161.70 KiB | **400.13 KiB** | 1.256 ms | 1.680 ms | 2.05 s | 587.6 s |
| SIFT100K | 256 -> 65,536 | 54 -> 863 | 0.99002 | 445.07 KiB | 152.45 KiB | **597.51 KiB** | 1.557 ms | 1.661 ms | 5.50 s | 1,002.8 s |
| HierGauss64-50K | 128 -> 2,048 -> 16,384 | 10 -> 23 -> 100 | 0.99250 | 44.31 KiB | 15.92 KiB | **60.23 KiB** | 0.190 ms | 0.302 ms | 0.80 s | 383.8 s |

所有行 `object_violations=0`。

## 4. 与旧 AutoVO 对比

| 数据 | 旧 AutoVO Recall / VO / 查询 | 新方法 Recall / VO / 查询 | VO 变化 | 查询变化 |
|---|---|---|---:|---:|
| SIFT20K | 0.99210 / 257.48 KiB / 1.930 ms | 0.99076 / 256.42 KiB / 0.777 ms | -0.41% | -59.7% |
| SIFT50K | 0.99044 / 397.76 KiB / 2.127 ms | 0.99198 / 400.13 KiB / 1.256 ms | +0.60% | -40.9% |
| SIFT100K | 0.99214 / 576.18 KiB / 3.881 ms | 0.99002 / 597.51 KiB / 1.557 ms | +3.70% | -59.9% |
| HierGauss64-50K | 0.99168 / 60.99 KiB / 0.175 ms | 0.99250 / 60.23 KiB / 0.190 ms | -1.24% | +8.3%（+0.015 ms） |

SIFT100K 相对手调 residual 2L（Recall 0.99021、547.08 KiB、3.686 ms）：VO 高 9.22%，查询快 57.8%，Recall 差 -0.00019 且仍满足 0.99 目标。因此仍落在此前设定的“VO 距手调最优不超过 15%”范围内。

结构没有简单地随数据量单调加深：20K 选两层，50K 选三层，100K 在更严格的 0.994 calibration 工作点又选回两层；HierGauss 选择另一种三层 fanout。这说明深度/fanout 确实由数据几何、规模和 Recall 约束共同决定。

## 5. 迭代记录

1. 初版固定比例逐次下降：5K smoke 自动得到 `32 -> 4096`、`10 -> 205`，Recall 0.9694、VO 122.15 KiB，但搜索 47.65 s。
2. 加入无收益深度停止与 fine clustering 复用：同一结果，搜索降到 32.76 s。
3. 每个步长只做一轮：搜索降到 21.98 s，但 HierGauss 某些三层路径的末级 probe 卡在约 610，不能适应强非均匀层间比例；该版本被否决。
4. 最终改为逐层区间二分 + 一次有限重分配：HierGauss 自动恢复到末级 probe=100；单候选常见 44--89 次评估，而旧重复下降常见 100--190 次。
5. 30-query HierGauss smoke 出现 calibration Recall=1、test Recall=0.9762，证明小校准样本不可靠；正式 HierGauss 改为 120 条后 test Recall=0.99250。

## 6. 是否达到目标

- 低 VO：达到。四组与旧 AutoVO 的差距均在 3.7% 内；与 SIFT100K 手调最优差 9.2%。
- Recall 不低于 0.99：四组达到，最低为 SIFT100K 的 0.99002。
- 查询代价不过大：达到。三组 SIFT 更快；HierGauss 只增加 0.015 ms。
- 选定索引构建成本可接受：达到，0.80--5.50 s。
- 自动搜索成本可接受：作为一次性离线原型可运行，但还不理想，为 273--1,003 s；SIFT50K/100K 比旧有限网格搜索更慢。
- 自适应性：初步达到。真实 SIFT 的三个规模与一个层次合成分布选出了不同深度、fanout 和 probe 配比。

综合判断：**方法 work，核心技术点足够集中，也能形成论文主线；当前最需要优化的不是在线协议，而是离线候选剪枝。** 不建议再引入神经预测器或 best-first 验证。下一步最有价值的是给结构候选增加安全的 VO lower bound，使明显劣势候选无需完成全部 Recall 校准。

## 7. 复现文件

- 实现：`joint_vo_successive_refinement.cpp`
- 构建入口：`Makefile` 中的 `joint_vo_successive_refinement_experiment`
- SIFT20K：`joint_refinement_bisect_sift20k.csv`
- SIFT50K：`joint_refinement_bisect_sift50k.csv`
- SIFT100K：`joint_refinement_bisect_sift100k.csv`
- HierGauss50K：`joint_refinement_hiergauss50k.csv`
- 迭代 smoke：`joint_refinement_smoke_*.csv`

