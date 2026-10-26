# 渐进式验证量化：可行性与查询代价

## 结论

`SQ4 -> SQ6 -> exact` **可以工作**：在不改变 IVF 返回结果的前提下，SIFT100K 相对现有 SQ4 完整 VO 再降低 **19.6%--26.6%**；SIFT1M 上降低 **12.1%**。将量化级联直接融合进查询扫描后，SIFT100K 的端到端查询计算只增加 **1.9%--2.6%**，SIFT1M 上反而加速 **7.4%**。

但它不应无条件启用。相对更强的 fixed interval-SQ6，渐进式的额外 VO 收益为 **1.8%--24.0%**，访问对象越多时收益越明显。因此正确的研究方向是 **latency-constrained adaptive progressive quantization**，而不是增加更多固定量化层。

## 实验设置与安全检查

- SIFT100K（50 queries）及 SIFT1M（10 queries），128 维，`k=100`；均重新计算精确 ground truth。
- 确定性 hierarchical IVF；三个 SIFT100K operating points，加一组 SIFT1M 规模验证。
- 最高精度为 6-bit；SQ4 是 SQ6 code 的严格前缀，每个 code 表示包含原坐标的向外舍入闭区间。
- 对所有被访问的 `(query, vector)`、SQ4/SQ6 下界逐一检查 `LB <= exact distance`，零 violation。
- 查询级联只用安全下界跳过 exact distance；每条 query 的完整 top-k ID 序列与 exhaustive IVF 完全相同。
- SIFT100K 时间为 7 次重复均值，SIFT1M 为 3 次；使用 query LUT、维度重排、partial-sum early rejection 和稀疏 multiproof layout。

## 核心结果

| Recall | IVF 参数 | visited | SQ4 exact | SQ4->6 exact | SQ4 VO | SQ4->6 VO | VO 降低 | 相对 fixed SQ6 | 查询计算增加 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 96.90% | 32768/384 | 1,500.5 | 491.5 | 193.6 | 451,205 B | 331,086 B | **26.62%** | 1.77% | **2.56%** |
| 98.40% | 32768/512 | 1,983.8 | 535.1 | 196.6 | 530,840 B | 396,074 B | **25.39%** | 3.01% | **2.43%** |
| 99.26% | 16384/640 | 5,076.6 | 597.1 | 200.0 | 798,936 B | 642,662 B | **19.56%** | 11.30% | **1.92%** |
| 98.70% (SIFT1M) | 16384/640 | 45,133.2 | 1,298.2 | 269.3 | 4,137,180 B | 3,637,540 B | **12.08%** | **24.03%** | **−7.37%** |

“查询计算增加”比较的是 `routing + progressive cascade scan` 与原始 `routing + exhaustive exact scan`。级联本身返回完全相同的结果。若把 VO 在查询结束后单独重新生成，相对 SQ4 verifier 的额外计算分别只有 `0.403/0.439/0.359 ms`；实际协议应缓存查询阶段已经计算的 lower bounds，避免第二遍扫描。

在 1 Gbps 网络下，前三组 VO 减少分别节省约 `0.961/1.078/1.250 ms` 传输时间，大于相对 SQ4 verifier 的额外计算。对应的粗略 break-even bandwidth 为 `2.39/2.46/3.48 Gbps`。

SIFT1M 上，原始 exact scan 为 `12.788 ms`；融合后的 SQ4->SQ6 scan 为 `11.106 ms`。它只计算约 685 个 exact distances，而不是访问的 45,133 个对象，因此减少原向量内存访问带来的收益超过了量化计算。此时相对 SQ4 verifier，VO 生成计算本身也少约 `1.214 ms`。

## 为什么只保留 SQ4 -> SQ6

以 Recall 96.9% 为例：

- `SQ4 -> SQ6`：331,086 B，proof computation 0.833 ms；
- `SQ3 -> SQ4 -> SQ6`：342,271 B，proof computation 1.163 ms。

SQ3 在 SIFT100K 上不能排除足够多的对象，VO 与时间同时变差。SIFT1M 上它能在 SQ4->SQ6 基础上再减 2.01% VO，但额外增加 `4.833 ms` 计算，明显不满足低 latency 目标。因此 SQ3 不应作为默认层，只能由 latency-aware optimizer 在低带宽场景选择。

## 自适应启用规则

令 `V` 为访问对象数，`S4` 为 SQ4 后仍重叠的对象数，`H4` 为 refinement multiproof bytes。相对 fixed SQ6，SQ4->SQ6 只有在下式成立时才节省 VO：

```text
32 * (V - S4) > H4
```

加入时间预算后的决策应为：

```text
32 * (V - S4) - H4 > lambda * (T_progressive - T_fixed6)
```

`lambda` 将 CPU 时间换算为系统成本。可以用前若干 posting lists 在线估计 `S4/V`，选择 fixed SQ6 或 SQ4->SQ6；预测不确定或 refinement budget 用尽时直接退化到 fixed SQ6/exact，不影响正确性。

## 当前边界

当前 VO 使用真实 disclosed set 和 SHA-256 multiproof **hash 数量**计费，但还没有序列化并实际生成/验证完整密码学 proof；因此字节结果可信，时间主要反映 bound evaluation 和 proof layout，而不是最终 client verification。服务端内存中的 code 当前按每维 1 byte 保存，而 VO 按 bit-packed 计费；最终系统还需测试 packed/SIMD decoder。SIFT1M 目前也只有 10 queries。下一步需要在 Deep/GIST 和至少 1,000 queries 上实现 one-shot layered proof，分别测量 serialization、hashing、network 和 client verification。
