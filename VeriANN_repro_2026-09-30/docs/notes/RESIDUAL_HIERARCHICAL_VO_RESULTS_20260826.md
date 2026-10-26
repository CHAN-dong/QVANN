# Stateless Verifiable ANN：低 VO 的两层残差索引与查询协议

> **后续更新：** 本报告的 583.81 KiB 两层方案仍是索引基线；进一步的
> 三层结构与认证 Huffman 协议实验见
> [FURTHER_VO_OPTIMIZATION_20260828.md](FURTHER_VO_OPTIMIZATION_20260828.md)。
> 最新最好结果为 561.08 KiB。

日期：2026-08-26  
数据：SIFT100K，128 维，`k=100`  
最终审计：前 1,000 条 SIFT query；200 条 learn query 只用于训练；客户端除公钥外不缓存 centroid、routing code、codebook、leaf directory 或验证边界。

## 0. 结论

在把“为什么选择这些簇”也完整计入每次 VO 后，当前找到的最好方案是：

- 65,536 个 fine IVF leaves；
- 把 fine centroids 再组织成 1,024 个 coarse groups；
- coarse centroid 用 64-byte SQ4，fine centroid 不独立编码，而写成 coarse base 加 24-byte PQ residual；
- 查询先由验证者重算全部 1,024 个 coarse code，确定 top-160；
- 只公开并认证这 160 个 group 的完整 residual-code slices，再由验证者确定这些组内的 top-768 fine leaves；
- 叶内继续用安全的渐进式对象证书，证明候选并集内的 exact top-100。

最终 1,000-query 结果：

| index / protocol | Recall@100 | route VO | leaf VO | total VO | ANN query | violations |
|---|---:|---:|---:|---:|---:|---:|
| IVF16k exact routing | 0.99022 | 8,244.14 KiB | 389.86 KiB | 8,634.00 KiB | 12.65 ms | 0 |
| flat IVF8k/PQ16 | 0.99131 | 178.49 KiB | 683.27 KiB | 861.76 KiB | 6.17 ms | 0 |
| **residual hierarchy 1k/65k** | **0.99073** | **449.33 KiB** | **134.48 KiB** | **583.81 KiB** | **3.71 ms** | **0** |

相对完整 flat PQ-IVF，新方案：

- Recall 只低 0.00058，仍高于 exact-routing 基线 0.00051，处在同一个约 0.99 level；
- 总 VO 降低 **32.25%**；
- ANN query time 降低 **39.85%**；
- 1,000 条查询、100,000 个 recall 判断中没有发现路由/对象安全证书违规。

相对 exact-centroid 的无缓存基线，总 VO 降低 **93.24%**，查询时间降低 **70.68%**。

这个收益不是“只把簇分细”。如果只分细，fine centroid 的公开成本会迅速增大。关键是同时改变索引表示和查询协议：相邻 fine centroids 共享一个 coarse base，数量庞大的 fine 部分只存 residual；每次只公开被正确 coarse routing 选中的 residual slices。

## 1. 安全目标和证明边界

验证者只持有数据拥有者公钥。每个 VO 都包含签名 index root/epoch 以及本次验证所需的全部静态参数。验证者检查：

1. coarse codes、所选 group descriptors、residual slices、fine leaf roots 和对象证书都属于同一个签名索引；
2. top-160 coarse groups 是按已认证 SQ4 表示和固定 tie-break 得到的确定性结果；
3. top-768 fine leaves 是按所选 group 中全部已认证 PQ residuals 得到的确定性组内结果；
4. 答案是这 768 个 fine leaves 并集内的 exact top-100；
5. 未精确返回的已访问对象都被安全距离下界排除。

协议保证的是“服务器忠实执行已签名的近似路由协议”，不是全局 exact-NN。相对全局 exact top-100 的质量由独立 Recall@100 实验衡量。这也是允许改变 ANN 查询协议之后，安全性与召回率之间应有的分界。

## 2. 索引结构

令 fine leaf `i` 属于 coarse group `g(i)`。它的路由中心表示为

```text
fine_center[i] ≈ decode(coarse_code[g(i)])
               + decode(residual_code[i])
```

认证结构如下：

```text
SignedIndexRoot / epoch
│
├── codec/verification-parameter commitment
│
├── CoarseCodeRoot
│   └── 1,024 × SQ4 code（64 B/code，查询时全部发送）
│
└── CoarseGroupDirectoryRoot
    ├── group descriptor g0（68 B）
    │   ├── residual-code root + length
    │   │   └── 约 64 × PQ24 residual code（24 B/code）
    │   └── fine-leaf-directory root
    │       ├── fine leaf root
    │       │   ├── 3/4-bit base certificates
    │       │   ├── 6-bit refinements
    │       │   └── lossless tails
    │       └── ...
    ├── group descriptor g1
    └── ...
```

平均每个 coarse group 有 64 个 fine leaves，每个 fine leaf 只有约 1.53 个对象。group descriptor 的 68 bytes 是两个 32-byte roots 加 4-byte length。

coarse 和 fine 使用非对称码率：

- coarse：SQ4，128 个 4-bit 标量，共 64 bytes。只有 1,024 个，适合花较多 bit 保住第一层 recall；
- residual：32 个 4-D subspaces、每个 64-entry codebook，共 32×6 bit = 24 bytes。它们数量大，但只发送所选 group 的 slices；
- residual PQ codebook 为 32,768 bytes，逐查询计入，没有假设缓存。

训练查询还只用于确定 Merkle 叶子的物理排列，使经常共同访问的 group/fine leaves 相邻。排列固化在签名索引中，不根据测试 query 变化，也不影响距离或 recall。

## 3. 查询和验证协议

```text
server                                              stateless verifier
  │  signed root + all codec/verification params           │
  │  + all 1,024 authenticated coarse codes                │
  ├────────────────────────────────────────────────────────>│ rebuild CoarseCodeRoot
  │                                                         │ compute all coarse distances
  │                                                         │ derive deterministic top-160
  │                                                         │
  │  160 group descriptors + directory multiproof          │
  │  + each selected group's COMPLETE residual-code slice  │
  ├────────────────────────────────────────────────────────>│ verify descriptors/slice roots
  │                                                         │ compute ~11,802 residual distances
  │                                                         │ derive deterministic top-768
  │                                                         │
  │  selected fine metadata + fine-directory multiproofs   │
  │  + object certificates + exact tails + claimed answer  │
  ├────────────────────────────────────────────────────────>│ verify leaf roots/lower bounds
  │                                                         │ accept exact top-100 in union
```

两个“完整”非常重要：

- 所有 coarse codes 都发送，因此服务器不能隐藏一个更近的 coarse group；
- 一个 coarse group 一旦被选中，其 residual slice 必须完整发送并重建该 group 的 residual root，因此服务器不能隐藏该 group 内更近的 fine leaf。

服务器不需要发送未选 coarse groups 的 residuals，因为协议定义本来就是 coarse top-160 后再 fine top-768。这一近似造成的全局漏召回体现在 Recall@100，而不是被伪装成密码学完备性。

fine leaf 可以按 `(coarse physical position, local physical position)` 重新编号，所以 residual slice 中不需要为约 11,802 个 code 再附带原全局 ID；只给最终 768 个 fine leaves 计 4 bytes/项的长度/位置元数据。

## 4. 完整 VO 账本

最终 residual hierarchy 的逐查询均值：

| 分项 | bytes/query | KiB/query | 说明 |
|---|---:|---:|---|
| routing/verification parameters | 50,656 | 49.47 | 见下方参数分解 |
| all coarse SQ4 codes | 65,536 | 64.00 | 1,024×64 B |
| selected residual slices | 283,253.232 | 276.61 | 11,802.218×24 B |
| group/fine metadata | 13,952 | 13.63 | 160×68 + 768×4 |
| routing Merkle multiproofs | 46,716.512 | 45.62 | coarse + selected fine |
| **routing subtotal** | **460,113.744** | **449.33** |  |
| leaf/object proof | 137,707.142 | 134.48 | 渐进下界、tails、local proofs |
| **total** | **597,820.886** | **583.81** |  |

50,656-byte 公共参数也逐查询发送：

```text
signed header/root                         96
coarse SQ4 min/step parameters          1,024
residual PQ24 codebook                 32,768
verification boundaries               16,640
per-dimension bit plan                    128
                                        ------
                                        50,656 bytes
```

验证树共有 128×65 个边界。SIFT 坐标为整数，训练算法产生的边界只能是整数或半整数，因此线上的规范编码使用 `uint16(2×boundary)`，范围 0..510。代码逐项断言可无损编码；这把原 8-byte double 账本从 66,560 bytes 降到 16,640 bytes，不改变任何比较、recall 或安全下界。

## 5. 为什么它确实能降 VO

flat PQ-IVF8k 的路由很便宜，但每次访问约 10,030 个对象，所以 leaf proof 是 683.27 KiB。单纯把叶子增到 65,536 个会让 leaf proof 下降，却会让全体 fine centers 的路由材料爆炸。

残差层次把这两个成本拆开：

```text
路由中心 payload
  = 1,024 × 64 B coarse bases
  + 平均 11,802 × 24 B selected residuals
  = 340.61 KiB
```

而不是发送：

```text
65,536 × fine-center-code
```

它使平均访问对象从 10,030 降到 1,287，leaf VO 从 683.27 KiB 降到 134.48 KiB，节省约 548.79 KiB。路由 VO 相对 flat 增加约 270.84 KiB，净收益仍为约 277.95 KiB，即总 VO 降低 32.25%。

因此真正的结构性收益来自三件事同时成立：

1. micro-leaves 让对象证书显著减少；
2. coarse base 在约 64 个 fine centers 之间共享，避免重复传输共同分量；
3. residual 只对已被验证者正确选中的 group 按需披露。

缺少第 2、3 点时，“分更细”本身不会得到这个结果。

## 6. 参数迭代和拐点

所有筛选都用同一确定性协议；下表总 VO 已补上紧凑编码后的 16.375 KiB leaf verification parameters。除最后一行外使用 identity Merkle layout。

| coarse probes | fine probes | Recall@100 | total VO |
|---:|---:|---:|---:|
| 256 | 672 | 0.99091 | 736.06 KiB |
| 224 | 672 | 0.99055 | 683.97 KiB |
| 192 | 704 | 0.99088 | 637.69 KiB |
| 160 | 736 | 0.99004 | 589.77 KiB |
| 160 | 768 | 0.99073 | 594.52 KiB |
| **160** | **768** | **0.99073** | **583.81 KiB（trace layout）** |

`coarse=128` 已成为无法由 fine probes 补救的瓶颈：fine probes 从 896 增至 1,024，Recall 只从 0.98852 升到 0.98951。故 160/768 是满足“Recall 不低于 0.99022 exact 基线”的最小稳定拐点，而不是事后随意挑出的 probes。

trace-aware 物理布局把同一 160/768 配置的 routing proof 从 57,688.288 降到 46,716.512 bytes，proof 降 19.02%，总 VO 再降 10.715 KiB；recall 和访问对象数完全不变。

编码扫描也显示需要非对称码率。100-query 筛选中：

- residual PQ16 只有约 0.956 recall；
- residual PQ32 可到约 0.985，但恢复到 0.99 时传输偏大；
- residual PQ24 在 coarse=256/fine=640 时已到 0.9931，是本数据上的 rate–distortion 拐点；
- coarse code 太短会在第一层不可逆地漏掉 group，所以只有 1,024 个 coarse codes 时，64-byte SQ4 比继续压 coarse 更合算。

## 7. 迭代过但不合适的方向

### 7.1 普通两层 IVF，共用同一种全局 centroid codec

它没有利用 `fine = coarse + residual`。短码使 coarse/fine 两次误差叠加；长码恢复 recall 后又要发送大量完整 fine centers。此前 SQ4 两层方案约 1,210 KiB，PQ32 在恢复约 0.99 recall 后也约 918 KiB，均被 flat 或 residual hierarchy 支配。

### 7.2 Authenticated best-first/beam tree

树上每个展开节点都必须公开并认证两侧 children。128 维中路径下界与近邻相关性不足：窄 beam recall 明显下降，宽 beam 的节点 records、proofs 和计算量同时膨胀。实验中 exact level-beam 虽到约 0.992 recall，但 VO 约 9.9 MiB、查询约 108 ms。

### 7.3 leaf 内普通 ball blocks/BVH

这是针对“明显不可能对象”的直接尝试：把每个 leaf 静态分成最多 4/8/16/32 个对象的 ball blocks，先发 108-byte authenticated block descriptor，安全下界失败才回退到逐对象证书。

在 IVF8k/PQ16/704 上，约 9,958 个访问对象中平均只整块排除约 34 个；descriptor 和 proof 已抵消收益，leaf VO 变化约为 -0.02%（更差）。较粗 IVF 的最好净收益也只有约 0.005%。原因不是验证不安全，而是 128 维 ball lower bound 太松，且靠近 top-k 阈值的一个对象就会打开整个 block。

所以“明显不可能对象”仍有优化价值，但普通欧氏 ball/BVH 不是合适的证书几何。

## 8. 还剩多少空间

当前 583.81 KiB 中：

- selected residual slices：276.61 KiB，占 47.4%；
- routing Merkle proofs：45.62 KiB，占 7.8%；
- 每查询静态参数 + coarse codes：113.47 KiB，占 19.4%；
- leaf/object proof：134.48 KiB，占 23.0%。

下一步优先级应当改变：对象 base certificates 已不再是绝对主项，最大项变成 residual slices。

较现实的继续优化方向：

1. **规范熵编码 residual slices。** slice 本来完整发送，可对 6-bit PQ symbols 做按 group/position 固定模型的 rANS/ANS；压缩字节串及长度一起承诺。若 residual payload 压 15%–25%，总 VO 可再降约 7%–12%。
2. **更好的低参数 residual transform。** 用固定 Hadamard/permutation、低参数 OPQ 或不等长 subspaces 提升第一层/残差 rate–distortion，目标是把 coarse probes 从 160 降到 128–144。完整 128×128 float rotation 要逐查询计 64 KiB，可能得不偿失，必须联合计费。
3. **PQ-subspace 对齐的 authenticated residual cover。** 对 residual code blocks 分 32 个子空间承诺上下界；只在可证明整个 block 不可能进入 fine top-768 时省略其 codes。它比 128-D 单球更贴合实际路由距离，但是否有净收益必须实验。
4. **更短的 routing membership proof。** 即使完全消除现有 Merkle proofs，上限也只是 45.62 KiB/7.8%；vector commitment 能改善，但会增加 verifier/prover 密码学成本。
5. **succinct routing proof。** SNARK/zkVM 可证明 top-160/top-768 计算并不公开 codes，理论上能再降一个数量级，但 prover latency 很难继续与 3–10 ms ANN 查询处在同一级别。

在继续坚持 stateless、SHA/Merkle、约 0.99 recall 和毫秒级 ANN routing 的条件下，预计仍有 **10%–25%** 的可信工程/结构空间；要再降 50% 以上，大概率要引入会话缓存、批查询摊销或 succinct computation proof，已经是新的系统假设。

## 9. 实现、复现与结果边界

实现：

- `residual_hier_vo.cpp`：两层 residual index、查询、trace layout 和完整路由账本；
- `complete_routing_vo.cpp`：exact/flat 完整基线、codec 和公共参数计费；
- `complete_block_vo.cpp`：普通 ball-block 失败方向；
- `final_compact_stateless_residual_c160_p768_q1000.csv`：最终结果；
- `final_compact_stateless_flat_pq16_n8192_p704_q1000.csv`：flat PQ 基线；
- `final_compact_stateless_exact_n16384_p640_q1000.csv`：exact routing 基线。

复现：

```bash
make complete_routing_vo_experiment residual_hier_vo_experiment

./residual_hier_vo_experiment \
  --dataset ../dataset/sift --base 100000 \
  --nlist 65536 --nprobe 768 \
  --coarse-nlist 1024 --coarse-nprobe 160 \
  --coarse-codec sq4 --residual-codec pq24 \
  --train-queries 200 --test-queries 1000 --k 100 --iters 6 \
  --output final_compact_stateless_residual_c160_p768_q1000.csv
```

原型实际执行近似路由、exact candidate scan 和逐对象安全下界检查；`violations=0`。Merkle multiproof 按树结构精确计 sibling hashes，签名/root 按 96 bytes 计。

仍需明确的工程边界：

- 内存中为查询速度保存的是 unpacked symbols，VO 账本使用规范 wire packing（SQ4 两个 nibble/byte，PQ24 四个 6-bit symbols/3 bytes）；production serializer 尚未实现；
- 当前没有实际序列化整个 transcript，也没有执行所有 SHA-256、签名验证或网络传输，所以毫秒数是 ANN routing / VO generation，不是端到端 verifier latency；
- 结论目前只在 SIFT100K 和一个固定训练/测试划分上验证，需要再用 SIFT1M、Deep1M/GIST、多个 seeds 检查泛化；
- 防回滚需要签名 epoch/freshness；96-byte header 计了 root/signature，但原型没有外部时间戳服务。

因此，这些结果足以说明索引/协议 Pareto 改善是真实的，并给出无缓存口径下的逐字节上限；要宣称 production cryptographic system，则还需要规范序列化、真实 verifier 和跨数据集复验。
