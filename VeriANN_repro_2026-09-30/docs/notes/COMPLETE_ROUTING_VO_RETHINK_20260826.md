# Stateless Verifiable ANN：把簇选择证明计入 VO 后的重新设计与实验

> **历史报告，结论已被取代。** 本文的完整 flat PQ-IVF 仍是有效对照，
> 但没有包括随后发现的两层残差路由方案，也曾用 8-byte double 计验证边界。
> 最新完整口径和最终建议见
> [RESIDUAL_HIERARCHICAL_VO_RESULTS_20260826.md](RESIDUAL_HIERARCHICAL_VO_RESULTS_20260826.md)。

日期：2026-08-26  
数据：SIFT100K，128 维，`k=100`  
最终审计：SIFT query 前 1,000 条；索引/量化规则使用 SIFT learn 200 条训练；所有最终结果均为客户端不缓存中心的逐查询 VO。  
本报告取代此前把中心视为已缓存而得到的 microcluster 结论。

## 1. 修正后的安全目标

验证者只预先持有数据拥有者签名的 index root/epoch，不持有任何 coarse/fine centroid、PQ codebook 或 leaf directory。每次查询的 VO 必须使验证者能够检查：

1. 路由材料确实属于签名索引；
2. 按协议定义的路由距离，服务器选择的簇恰好是确定性的 `top-nprobe`，不能漏掉更近的簇；
3. 返回结果是所选簇并集内的精确 top-k；
4. 每个没有返回的已访问对象都有安全下界证书，或被一个安全的整组下界排除；
5. 量化路由允许改变 ANN 候选集，因此它不证明全局精确 top-k；相对全局精确 top-k 的质量由独立 recall 实验衡量。

防回滚还需要签名 epoch/freshness；本实验的 96-byte header/signature 计费包含 root/signature，但不实现外部时间戳服务。

## 2. 索引和协议

### 2.1 最终方案：Flat authenticated IVF + compressed complete routing

最终选中的索引不是 65,536 个 microclusters，而是 8,192 个 IVF leaves。索引 root 分别承诺：

```text
SignedIndexRoot
├── routing parameters / PQ codebook root
├── routing-code array root           # 8,192 × 16 B
└── permuted leaf-directory root       # 每项绑定 leaf root、长度、摘要类型
    ├── leaf root 0
    │   ├── 可选的安全球/量化盒摘要
    │   ├── 3/4-bit base object certificates
    │   ├── 6-bit refinement certificates
    │   └── lossless exact tails / object positions
    └── ...
```

这里 `PQ16` 表示每个中心总共 16 bytes，而不是每维 16 bit：128 维被切为 32 个 4 维 subspaces，每个 subspace 使用 16-entry（4-bit）codebook。codebook 为 8,192 bytes；它也逐查询发送，没有隐含缓存。

查询/验证流程：

```text
server                                        stateless verifier
  │  signed root + PQ codebook + all routing codes  │
  ├─────────────────────────────────────────────────>│ reconstruct routing root
  │                                                  │ compute 8,192 ADC distances
  │                                                  │ derive deterministic top-704
  │ selected leaf IDs + leaf-directory multiproof    │
  ├─────────────────────────────────────────────────>│ compare with recomputed top-704
  │ answer, tau, summaries/object certificates       │
  ├─────────────────────────────────────────────────>│ verify leaf roots and safe lower bounds
  │                                                  │ accept exact top-100 in candidate union
```

服务器不能通过少发中心来改变簇选择，因为完整 routing-code array 是 VO 的一部分，验证者重建其 commitment 并自己计算 `top-704`。被选择的 leaf descriptors 用 SHA-256 Merkle multiproof 连接到独立的 leaf-directory root。

leaf directory 的物理顺序由 200 条训练查询学习，规则随后固化并由 index root 签名；测试查询不改变布局。它只减少共同访问 leaves 的 multiproof sibling hashes，不影响簇选择或安全性。

### 2.2 完整 VO 公式

Flat IVF 每查询路由 VO：

```text
VO_route = 96
         + PQ_codebook_bytes
         + nlist × routing_code_bytes
         + nprobe × 4                       # leaf lengths/IDs
         + selected_leaf_multiproof_hashes × 32
```

叶 VO：

```text
VO_leaf = safe_summary_payload
        + base_certificate_payload
        + refinement_payload
        + lossless_exact_tail_payload
        + local Merkle multiproofs
```

最终方案的实测均值：

| 分项 | bytes/query | KiB/query |
|---|---:|---:|
| signed header + PQ codebook | 8,288 | 8.09 |
| 8,192 routing codes | 131,072 | 128.00 |
| selected leaf metadata | 2,816 | 2.75 |
| leaf-directory multiproof | 23,829 | 23.27 |
| **routing subtotal** | **166,005** | **162.11** |
| leaf summaries | 2,729 | 2.67 |
| object base certificates | 539,194 | 526.56 |
| refinements | 54,033 | 52.77 |
| lossless exact tails | 2,800 | 2.73 |
| local Merkle proofs | 100,917 | 98.55 |
| **leaf subtotal** | **699,673** | **683.27** |
| **total** | **865,678** | **845.39** |

## 3. 最终 1,000-query 结果

三行都包含簇选择证明和对象证明；`violations=0` 表示安全下界逐对象检查和组摘要检查均未发现错误。

| index / protocol | Recall@100 | route KiB | leaf KiB | total KiB | ANN query ms | violations |
|---|---:|---:|---:|---:|---:|---:|
| IVF16k, exact float centers, nprobe=640 | 0.99022 | 8,227.77 | 389.86 | 8,617.63 | 12.66 | 0 |
| IVF16k, PQ16, nprobe=1088 | 0.99111 | 308.93 | 564.96 | 873.88 | 6.92 | 0 |
| **IVF8k, PQ16, nprobe=704** | **0.99131** | **162.11** | **683.27** | **845.39** | **6.61** | **0** |

相对完整、无缓存、精确中心的正确基线，最终方案：

- recall 高 0.00109；
- ANN 查询时间低 47.8%；
- 总 VO 低 90.19%；
- routing VO 低 98.03%。

但必须把收益来源分开：同样使用 PQ16 和学习布局时，IVF16k 改为 IVF8k 只把 873.88 KiB 降到 845.39 KiB，即 **3.26%**。约 90% 的大幅下降主要来自协议把“按 512-byte 精确中心路由”改成“按已认证的 16-byte PQ code 路由”，不是一个巧妙的层次索引结果。

一个略低 recall 的 Pareto 点是 IVF8k/PQ16/nprobe=672：Recall 0.99014、819.45 KiB、5.15 ms。它比精确基线少 8/100,000 个 recall hits，因此没有选作严格的最终点。

此前忽略中心 VO 的旧 IVF16k 基线约为 429.82 KiB；最终完整方案 845.39 KiB 反而高 96.7%。所以此前“microcluster 降低约 70% VO”的结论在 stateless 完整计费下无效，不能继续引用。

## 4. 迭代过但被淘汰的方向

以下为 100-query 参数探索，用来筛方向；数值不替代上面的 1,000-query 最终审计。

### 4.1 65,536 fine leaves + 1,024 coarse groups

协议发送全部 coarse codes、所选 coarse group descriptors/multiproof、所选组内的完整 fine-code slices，以及最终 fine leaf proofs。

| routing codec / probes | Recall | total KiB | query ms | 判断 |
|---|---:|---:|---:|---|
| PQ8, coarse224/fine416 | 0.8518 | 268.54 | 2.02 | VO 小但 recall 不合格 |
| PQ16, coarse224/fine416 | 0.9333 | 403.53 | 3.39 | 不合格 |
| PQ32, coarse224/fine416 | 0.9727 | 672.01 | 5.31 | 不合格 |
| SQ4, coarse224/fine416 | 0.9935 | 1,210.44 | 13.59 | recall 恢复但比 flat 更大、更慢 |
| PQ32, coarse320/fine640 | 0.9907 | 918.08 | 7.62 | 被 flat Pareto 点支配 |
| PQ32, coarse384/fine768 | 0.9941 | 1,066.79 | 9.43 | 被 flat Pareto 点支配 |

结论：两层结构减少叶对象数，但必须公开很多 fine routing codes；中心码过短时 coarse/fine 两次量化误差叠加，恢复 recall 后路由 VO 又超过 flat。

### 4.2 Authenticated binary best-first / level-beam tree

每次展开一个内部节点，VO 发送两个 authenticated child records；验证者维护同样的 priority queue 或 beam，因此服务器不能隐藏分支。

| tree protocol | codec | Recall | total KiB | query ms |
|---|---|---:|---:|---:|
| best-first, 416 leaves | exact | 0.8359 | 1,643.75 | 1.23 |
| best-first, 416 leaves | PQ16 | 0.6900 | 182.36 | 0.61 |
| level beam=1024, 416 leaves | PQ32 | 0.9666 | 1,297.75 | 59.61 |
| level beam=1024, 416 leaves | exact | 0.9920 | 9,899.77 | 107.98 |

结论：128 维中树路径与真实近邻相关性不足。宽 beam 能恢复 recall，但每层必须公开两侧 child commitments/codes，VO 和时间都失控。

### 4.3 叶级单摘要

对反复访问且训练期预期收益为正的 leaf 承诺安全球或量化包围盒。测试时只有 `LB(q, leaf) > tau` 才整叶剪枝，否则回退到逐对象证明。

在 IVF16k/PQ16/nprobe=1024 的 100-query 实验中，平均只安全剪掉 101/7,265 个对象；摘要 payload 抵消大部分节省，总 VO 仅由 827.01 KiB 降为 825.87 KiB（约 0.14%）。原因是叶内任一靠近阈值的对象都会阻止整叶剪枝。

这说明“明显不可能对象”确实仍是最大 payload，但单个 leaf summary 的粒度太粗；需要 leaf 内 query-aware BVH/block cover，而不是单纯增加 IVF leaves。

## 5. 结论与剩余空间

### 当前建议

在“无客户端缓存、hash/Merkle 型证明、查询效率和 recall 与原方案同级”的约束下，当前最合适的是：

1. 保持单层 IVF，但联合选择 `nlist/nprobe`，不要默认越细越好；SIFT100K 的拐点在本次扫描中约为 IVF8k/PQ16/704。
2. 路由 correctness 定义在签名的 PQ codes 上；每查询发送完整 route-code array，由验证者重算 top-nprobe。
3. leaf roots 独立承诺并使用训练查询固定的 trace-aware Merkle 排列。
4. 叶对象使用 3/4-bit base → 6-bit refinement → lossless tail 的安全渐进证书。
5. whole-leaf summary 只作为可选小优化，不应宣称为主贡献。

### 还有多少空间

当前 845 KiB 中 683 KiB（80.8%）已经是 leaf proof，尤其 527 KiB 是对象 base certificates。继续压缩 centroid 或 Merkle routing proof 的上限有限：即便把 23.3 KiB leaf-directory multiproof 完全消掉，总 VO 也只再降 2.8%。

仍值得做的方向按优先级排序：

1. **Leaf 内 query-aware authenticated BVH/block cover。** 训练只决定静态分组；查询仍用安全 lower bound。它直接针对 527 KiB base payload。已有独立 batch 实验说明收益高度依赖 list size/query workload；必须在完整 routing VO 下重新联合扫描较粗 IVF 与 block size。现实预期是个位数到约 15%，不是数量级下降。
2. **更好的 8–16 byte routing representation。** 当前 PQ8 需要大量 probes，PQ16 是实测拐点。OPQ/learned rotation、非均匀 bit allocation 可能在相同 recall 下少开 leaves，预期有 5%–15% 总 VO 空间，但必须把 rotation/codebook 也逐查询计费。
3. **Vector commitment 替换 leaf-directory Merkle。** 可把约 23 KiB routing proof 降到较小证明，但引入配对/可信设置或更贵验证；总收益上限约 3%。
4. **Succinct proof of routing computation。** 用 SNARK/zkVM 证明对 8,192 个 codes 的 ADC+top-k，可不发送完整 routing array，VO 有机会降到 KiB 级；但 prover 必须处理约百万维度运算，目前很难保持 5–10 ms 查询效率。这是若要再降一个数量级最明确、也最昂贵的协议变化。

低扇出层次树、简单 microcluster 和单 leaf summary 已经被实验否定。若限制为 stateless、SHA/Merkle、约 0.99 recall 和约 10 ms 查询，本数据上的剩余可靠空间更像 **10%–20%**，而不是再有 70% 的结构性下降；要获得数量级下降，需要允许客户端会话状态、批查询摊销，或使用 succinct computation proofs。

## 6. 实现、原始数据与复现

- 完整 flat/two-stage routing 实验：`complete_routing_vo.cpp`
- authenticated tree/beam 失败方向：`best_first_auth_tree.cpp`
- 最终精确基线：`final_complete_flat_exact_n16384_p640_q1000.csv`
- 同召回 IVF16k/PQ16：`final_complete_flat_pq16_n16384_p1088_q1000.csv`
- 最终 IVF8k/PQ16：`final_complete_flat_pq16_n8192_p704_q1000.csv`
- Pareto 点：`final_complete_flat_pq16_n8192_p672_q1000.csv`

最终方案复现命令：

```bash
make complete_routing_vo_experiment
./complete_routing_vo_experiment \
  --base 100000 --nlist 8192 --nprobe 704 --codec pq16 \
  --train-queries 200 --test-queries 1000 --iters 6 \
  --pq-train 8192 --pq-iters 5 \
  --output final_complete_flat_pq16_n8192_p704_q1000.csv
```

## 7. 结果边界

本原型实际执行 ANN routing、exact candidate scan、量化安全下界检查，并对 1,000 条查询逐对象检查 `lower_bound <= exact_distance`，最终 violations 为 0。VO 中 SHA-256 multiproof 数量按完整树精确计数，签名/root 按 96 bytes 计费。

它仍不是 production cryptographic implementation：实验没有实际序列化每个 VO、执行所有 SHA-256 或签名验证，`ann_query_ms` 不包含网络、序列化和完整客户端加密验证；`vo_generation_ms` 主要是证书筛选/安全检查时间。因此可据此判断算法 Pareto 和字节量，不应把毫秒数解释为端到端网络延迟或完整 verifier latency。
