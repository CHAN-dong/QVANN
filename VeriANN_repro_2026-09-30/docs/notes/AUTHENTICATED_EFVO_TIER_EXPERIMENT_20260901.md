# 单参数认证 Probe 策略实验

日期：2026-09-01

## 1. 结论

最终方案可以工作，而且在当前四组实验上效果可观。合适的实现不是 best-first，也不是把一个大预测模型放进每个 VO，而是：

> 对每棵已构建的 residual tree，离线生成一个很小的、经过认证的 `efVO tier -> 每层 probe 向量` 策略表；在线查询仍执行普通的逐层 top-p。

在默认档位上：

- 四组 `Recall@100` 为 0.99016--0.99178；
- 相对此前固定 probe 的 AutoVO 结果，Recall 最大下降 0.00076，即 0.076 个百分点；
- 总 VO 最大增加 2.50%，一组略有下降；
- 实测 ANN 查询没有出现延迟回退；
- 两层表为 64 B，三层表为 84 B。表随索引承诺并缓存时，每查询增加 0 B；即使无状态重传，也只增加 0.063/0.082 KiB；
- 四组完整叶证明检查的 `object_violations` 均为 0。

因此，它达到了“提供一个简单参数控制查询效率/Recall，同时基本不破坏现有 VO、延迟和验证结构”的目标。边界是：Recall 只能作分布统计校准，不能对单条未知查询给出确定性保证。

## 2. 为什么最终采用档位表

实验先实现了一个更紧凑的光滑模型：

```text
p_l(B) = anchor_l * exp(beta1_l * log(B/B0)
                        + beta2_l * log^2(B/B0))
```

SIFT20K/50K/100K 的默认档位与校准候选最优点非常接近；但在 HierGauss 的低预算档，层间最优分配发生跳变。光滑模型在 0.9 档得到 Recall 0.94968，而小表选择为 0.98082。继续增加模型复杂度会降低可解释性，也会放大客户端验证负担。

最终改为五档策略表。它不是神经网络；每档直接存 `(p_0,...,p_{L-1})`。客户端只提交一个档位编号，服务端和验证者都从已认证的表得到同一个 probe 向量。这个改动消除了分布迁移下的拟合误差，同时不改变查询协议。

## 3. 离线构造

本实验复用 AutoVO 已经自动选出的 residual tree 结构；这里验证的是单参数 probe 控制，不重新搜索树深和分支数。

1. 以原 AutoVO probe 向量为中心，生成全局 scale 和逐层 tilt 的小候选集。
2. 在 120 条 calibration queries 上测每个候选的 Recall。
3. 成本采用实际 routing 字节公式加保守 leaf proxy：

```text
C_proxy(p) = C_route(p) + visited_objects(p) * leaf_unit
```

4. 对五个相对成本档 `{0.75, 0.90, 1.00, 1.15, 1.30}`，选择预算内 calibration Recall 最高的 probe 向量。
5. 将五个向量与索引元数据一起承诺。两层需要 64 B，三层需要 84 B。
6. 最终报告不用 proxy 冒充结果，而是在独立的 500 条 test queries 上生成完整 routing + progressive leaf VO。

这里不依赖热门查询频率或在线访问日志，但使用独立查询样本校准 Recall，因此准确说是 distribution-sample-aware，而不是严格的 zero-query workload-free。

## 4. 在线查询与验证

用户只给一个 `efVO tier`：

```text
tier e
   -> authenticated policy[e]
   -> (p0, p1, ..., pL-1)
   -> ordinary stagewise top-p traversal
```

查询过程：

1. 根层计算所有 root codes 的量化距离，取 top-`p0`。
2. 在这些父节点的孩子中计算 residual 量化距离，整体取 top-`p1`。
3. 逐层重复；末层选择 top-`pL-1` 叶簇。
4. 只对所选叶簇内对象做精确距离计算，产生 top-k。
5. 用已有 Merkle multiproof、量化路由码和 progressive leaf certificate 生成 VO。

验证者不维护 best-first 优先队列，也不做动态停止。它从策略表确定每层的 `p_l`，然后验证普通的逐层 top-p 关系和叶对象证明。因此客户端复杂度与原固定 probe 协议相同，只多一次 O(L) 的表读取。

## 5. 实验设置

- `k=100`；
- calibration queries：120；
- 独立 test queries：500；
- SIFT：20K、50K、100K base；
- 第二种几何：HierGauss64，50K base；
- 根 codec 为 SQ4，两层 residual 为 PQ24；三层 middle/fine 为 PQ24/PQ16；
- 完整 VO 包括 codec 参数、路由码、metadata、Merkle proofs、渐进叶摘要、精确对象和 framing；
- KiB 按 1024 bytes；
- 策略表视为索引级缓存，未加到每查询总 VO；无状态重传增量最多 0.082 KiB。

## 6. 默认档位结果

| 数据 | 结构 | probes | Recall@100 | Routing VO | Leaf VO | 总 VO | ANN 查询 | VO 生成 | 索引构建 | 策略搜索 | 表大小 |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| SIFT20K | 512 -> 16,384 | 133 -> 360 | 0.99154 | 210.27 KiB | 53.65 KiB | **263.92 KiB** | 1.093 ms | 0.651 ms | 1.10 s | 7.62 s | 64 B |
| SIFT50K | 512 -> 32,768 | 102 -> 721 | 0.99016 | 278.11 KiB | 127.48 KiB | **405.59 KiB** | 1.338 ms | 1.346 ms | 2.62 s | 10.14 s | 64 B |
| SIFT100K | 256 -> 4,096 -> 65,536 | 102 -> 653 -> 1,254 | 0.99138 | 380.71 KiB | 202.63 KiB | **583.34 KiB** | 3.316 ms | 2.227 ms | 6.32 s | 73.19 s | 84 B |
| HierGauss64-50K | 64 -> 1,024 -> 16,384 | 5 -> 30 -> 100 | 0.99178 | 44.79 KiB | 15.95 KiB | **60.73 KiB** | 0.159 ms | 0.246 ms | 0.84 s | 2.67 s | 84 B |

三层 100K 的策略搜索明显更贵，因为原型枚举了 162 个候选，且尚未缓存不同 probe 向量共享的中心分数。73 s 对静态索引仍是可接受的一次性离线成本，但动态高频更新场景需要候选缓存或坐标搜索。

## 7. 参数确实形成有效权衡

| 数据 | 低档：Recall / VO / 查询 | 默认档：Recall / VO / 查询 | 高档：Recall / VO / 查询 |
|---|---|---|---|
| SIFT20K | 0.95622 / 193.62 KiB / 0.700 ms | 0.99154 / 263.92 KiB / 1.093 ms | 0.99804 / 320.03 KiB / 1.334 ms |
| SIFT50K | 0.96652 / 301.23 KiB / 0.934 ms | 0.99016 / 405.59 KiB / 1.338 ms | 0.99712 / 516.70 KiB / 1.863 ms |
| SIFT100K | 0.97810 / 446.51 KiB / 2.313 ms | 0.99138 / 583.34 KiB / 3.316 ms | 0.99684 / 729.90 KiB / 4.046 ms |
| HierGauss64-50K | 0.92538 / 53.82 KiB / 0.109 ms | 0.99178 / 60.73 KiB / 0.159 ms | 0.99514 / 73.40 KiB / 0.239 ms |

五档的完整数据在相应 CSV 中。Recall、VO 和查询开销整体随档位上升，说明该参数具备类似 IVF `nprobe` 的工程含义。预算比例是候选搜索的 proxy tier，不应解释成严格的“实际 VO 字节上限”；若 API 需要硬字节预算，应按完整 VO 的保守上界重新标定档位。

## 8. 与此前固定 probe AutoVO 的默认点比较

| 数据 | Recall 差值 | 总 VO 差值 | 实测查询差值 |
|---|---:|---:|---:|
| SIFT20K | -0.00056 | +2.50% | -43.4% |
| SIFT50K | -0.00028 | +1.97% | -37.1% |
| SIFT100K | -0.00076 | +1.24% | -14.5% |
| HierGauss64-50K | +0.00010 | -0.42% | -9.1% |

本实验使用统一的任意深度 stagewise evaluator 和 identity routing layout；此前两层完整 evaluator 使用了学习后的 Merkle layout。因此 VO/延迟比较是回归参考，不是微观实现完全相同的消融。更可靠的结论是：同一棵树和同等级 probe 下，没有观察到实质 Recall 或 VO 退化；最大 Recall 差小于 0.08 个百分点，最大 VO 差为 2.5%。

## 9. 能保证什么

可以由协议保证：

- 档位表被索引 commitment 绑定后，服务端不能为同一档位任意换 probe 向量；
- 每层仍是确定的 top-p 规则，不需要验证动态队列轨迹；
- 当前完整叶证明测试中，四组 `object_violations=0`。

不能由协议确定性保证：

- ANN Recall 是相对真实 top-k 的分布指标，不能仅靠 Merkle proof 保证；
- 当前表的 Recall 是 120 条 calibration queries 选择、500 条独立 queries 验证得到的统计结果；
- 对显著 distribution shift 或 adversarial query，仍需重新校准或给出保守安全档。

另外，当前代码核算了完整证明字节并执行了量化边界覆盖检查，但还没有实现一个与服务端完全独立、可注入恶意 transcript 的端到端 verifier。论文中不应把 `violations=0` 表述成已经完成 adversarial soundness 实现。

## 10. 适用性

适合：

- 静态或低频更新的 residual IVF tree；
- 希望用一个简单参数控制 VO/延迟/Recall；
- 客户端不能接受 best-first 队列重放；
- 策略元数据可以随索引缓存。

仍需补充：

- GIST、Deep、Glove 等至少两个标准真实数据集；
- 多随机种子和置信区间；
- calibration query 数量消融；
- 100K 以上规模的候选分数缓存和早停；
- 独立 adversarial verifier；
- distribution shift 检测与重新校准策略。

## 11. 可复现文件

- 实现：`compact_probe_allocator.cpp`
- 构建目标：`compact_probe_allocator_experiment`
- SIFT20K：`compact_allocator_table_sift20k.csv`
- SIFT50K：`compact_allocator_table_sift50k.csv`
- SIFT100K：`compact_allocator_table_sift100k.csv`
- HierGauss64-50K：`compact_allocator_table_hiergauss.csv`
- 上一阶段基线：`ADAPTIVE_RESIDUAL_VO_EXPERIMENT_20260901.md`

## 12. 最终判断

这个方法在当前范围内是 work 的，并且比 best-first 更符合验证协议：一个用户参数、固定逐层 top-p、极小认证策略元数据、没有队列 transcript。默认档对现有 AutoVO 的影响很小，五档又提供了明确的 Recall/VO/延迟权衡。

论文中最稳妥的表述是“authenticated calibrated probe policy”，而不是“Recall prediction model”。它解决的是可验证的参数映射和实用权衡；Recall 保证仍是统计校准问题。
