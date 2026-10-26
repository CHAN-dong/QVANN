# Workload-aware multi-level quantization：最终实验结论

## 结论

普通量化不是 VO 的理论极限，但在原来的“量化过滤 + float exact disclosure”协议里，继续只训练量化边界已经接近局部极限。真正的大瓶颈是：前面把向量压成 4/6 bit，最后一级又按 128 个 float（512 B）发送原向量。

本轮把末级改成可逆整数 tail，并联合优化 ID 和 Merkle proof。严格由训练集选出的方案将 VO 从当前最强可比基线的 **538.69 KiB/query 降至 416.64 KiB/query，再降 22.66%**。相对原始未量化实验的 2,591.63 KiB/query，累计下降 83.92%。Recall@100 保持 0.9939，安全违规为 0。

## 量化方法的完整对比

历史公平实验使用 SIFT100K、128 维、IVF `nlist=16384`、`nprobe=640`、`k=100`，100 个训练 query 和 100 个独立测试 query。

| 方法 | VO/query | 相对上一阶段 | 相对未量化 |
|---|---:|---:|---:|
| 未量化 | 2,591.63 KiB | - | - |
| 普通 uniform SQ6 | 689.46 KiB | -73.40% | -73.40% |
| 训练型 VT-SQ6 | 681.39 KiB | -1.17% | -73.71% |
| VT SQ4 → SQ6 | 583.34 KiB | -14.39% | -77.49% |

最新实验使用 200 个官方 `sift_learn` query 训练、100 个 `sift_query` query 测试。测试集不参与参数和协议选择。

| 同轮 held-out 消融 | VO/query | 相对前一项 | 相对 trace VT |
|---|---:|---:|---:|
| trace-aware VT SQ4 → SQ6 | 538.69 KiB | - | - |
| workload-aware 3/4 bit → SQ6 | 525.69 KiB | -2.41% | -2.41% |
| + lossless integer tail | 441.54 KiB | -16.01% | -18.03% |
| + position-addressed ID | 422.76 KiB | -4.25% | -21.52% |
| + public-dummy multiproof | **416.64 KiB** | -1.45% | **-22.66%** |

测试集上的 fused commitment 为 414.56 KiB，但训练集没有选中它，因此不把这个数字作为最终结果。

## 最终方案训练什么

训练参数保持低自由度，避免按 10 万对象训练造成过拟合：

- 6-bit verification-tree 边界：每个维度一棵嵌套标量树，优化 top-k 阈值附近的 certified-distance slack。
- 维度降位顺序：128 个维度的 harm score；从 `{0,8,16,24,32,48,64,80,96,112,128}` 中选择从 4 bit 降到 3 bit 的维度数。最终选择 48 个维度，base code 为 464 bit，即 58 B。
- routing Merkle 排列：用训练 query 的 list 共访问 trace 学习相邻布局。
- commitment 形式：训练数据在 independent exact proof 与 fused commitment 之间选择；最终选择 independent。

不训练 per-object bit。此前的 per-object 方案在 held-out 上比 trace VT 反而高 0.41%，说明约 10 次访问/对象不足以支撑 10 万个独立决策。

## 训练目标与流程

目标不是重构误差，而是验证者实际接收的平均 VO：

```text
VO = base payload
   + refinement payload
   + lossless tail / exact payload
   + local Merkle multiproofs
   + routing multiproof
```

约束是所有量化 lower bound 保守、Recall 不因证书改变、held-out 安全违规为 0。

流程：前 100 个 learn query 估计维度 harm；后 100 个 learn query 选择降位数量；再用全部 200 个 learn query 重估顺序；最后只在 100 个 test query 上报告结果。最终协议选择同样只看 learn query。

## 为什么 lossless tail 有效

SIFT 坐标本来就是 `[0,255]` 整数。获得 6-bit VT cell 后，不需要再发送 512 B float 向量，只需发送原整数在该 cell 的 rank。alphabet 完全由公开 VT 边界确定，不需要把额外 codebook 放入 VO。

- 对全部 100,000 个向量执行了实际 bit-pack 和 decode 自检，逐坐标无损重构。
- tail 平均为 12.13 B/vector；当前 exact 集的 tail 加 ID 仅 2.54 KiB/query。
- list 内位置已经被 Merkle proof 认证，因此 base record 不再重复携带 4 B ID；外部 ID 只随末级记录发送。

## 最终 VO 瓶颈

最终 416.64 KiB/query 的组成是：

| 项 | KiB/query | 占比 |
|---|---:|---:|
| base code | 282.02 | 67.7% |
| local Merkle proof | 67.05 | 16.1% |
| routing | 36.08 | 8.7% |
| SQ6 refinement | 28.96 | 7.0% |
| lossless tail + ID | 2.54 | 0.6% |

因此下一步最有潜力的方向已经改变：应减少 visited-object base payload，或联合优化 IVF granularity、base bit budget 和 local Merkle layout。继续微调 exact 层最多只影响 0.6%，不会再带来大幅下降。

原始结果见 `workload_hybrid_lossless_vo_sift100k_20260825.csv.protocol.csv`，实现见 `workload_hybrid_vo.cpp`。
