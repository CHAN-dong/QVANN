# Query-aware IVF Batch Exclusion

## 独立训练/测试协议

- 测试 query：SIFT query 0–49；
- 训练 query：默认 50–249（200 条）；
- 测试 query 不参与 block 构造；
- 每个 `nprobe` 独立学习一个静态、可认证的分块布局；
- 候选 block 来自 posting list 内的几何 Ball Tree，最大 16 个对象。

对每个候选区域，bottom-up dynamic programming 在三种动作中选择训练 workload 上预计 VO 最小者：

1. 合并为一个 108-byte 认证 block；
2. 保留左右子块；
3. 不使用批量摘要，退回现有的 72-byte SQ4 object records。

训练成本直接计为：

```text
block: 108 + I(LB <= tau) * (72 * count + 512 * exact_open)
raw:   72 * count + 512 * exact_open
```

机器学习/历史 workload 只决定布局；查询时仍然必须满足安全条件 `LB(q, block) > tau` 才能排除，因此预测或泛化失败只会使 VO 变大，不会产生错误结果。

## SIFT100K，k=100，200 条训练 query

| nprobe | 训练 VO 降低 | 独立测试 VO 降低 | 固定 block=16 测试结果 |
|---:|---:|---:|---:|
| 8 | 0.34% | **0.15%** | −9.92% |
| 16 | 0.58% | **0.39%** | −11.50% |
| 32 | 1.14% | **0.92%** | −12.38% |
| 64 | 3.97% | **4.40%** | −6.17% |
| 100 | 18.09% | **19.24%** | 16.25% |

固定 block 列中的负号表示 VO 增大。Query-aware DP 在低 `nprobe` 主动让绝大多数区域退回 raw SQ4 records，避免强制 block 摘要的负收益；在高 `nprobe` 则保留可共同排除的 blocks。

仅使用 50 条训练 query 时，`nprobe=8/16/32` 的测试结果分别为 −0.43%、−0.58%、−0.68%，出现轻微过拟合；增加到 200 条后全部转为正收益。这说明 workload 样本量是必要条件。

## SIFT1M，k=100，nprobe=100

使用 20 条训练 query、10 条独立测试 query：

| 方法 | 测试 VO 降低 |
|---|---:|
| 固定相似 block=16 | 28.37% |
| Query-aware max-block=16 | **29.23%** |

训练集降低 34.56%，测试集降低 29.23%，存在过拟合，但仍比固定 block 多约 0.86 个百分点。需要更多训练 query 才能稳定估计 1M workload 的真实增益。

## 结论

Query-aware 有效，主要价值不是让所有 block 的过滤率变高，而是学习“哪里不应该使用 block”：

- 中低 `nprobe`：把大多数区域退回 raw SQ4，获得约 0.15%–0.92% 的小幅净收益，而不是固定分块的 10%–12% 负收益；
- `nprobe=64`：把原来的 6.17% 负收益转为 4.40% 正收益；
- `nprobe=100`：相对固定 block=16 再提高约 1–3 个百分点。

完整结果见 `query_aware_batch_results_sift100k_k100.csv`、`query_aware_batch_results_sift100k_k100_train200.csv` 和 `query_aware_batch_results_sift1m_k100_q10_train20.csv`。

