# Verifiable ANN 进一步 VO 优化：三层残差与认证熵编码

日期：2026-08-28  
口径：SIFT100K、前 1,000 条 query、`k=100`、stateless verifier；所有 codebook、模型、验证参数、metadata 和 multiproof 均逐查询计费。

## 结论

最终保留两层 residual hierarchy，但把 coarse/residual wire streams 改成已认证 canonical Huffman：

| 方案 | Recall@100 | route VO | leaf VO | total VO | ANN query | violations |
|---|---:|---:|---:|---:|---:|---:|
| 两层固定码 | 0.99073 | 449.33 KiB | 134.48 KiB | 583.81 KiB | 3.55–3.71 ms | 0 |
| **两层 + canonical Huffman** | **0.99073** | **426.60 KiB** | **134.48 KiB** | **561.08 KiB** | **3.59 ms*** | **0** |

相对固定码再降 22.73 KiB/3.89%；相对完整 flat PQ-IVF 的 861.76 KiB 降 34.89%。  
* 查询时间没有执行 Huffman serializer/decoder，只说明 ANN 路由路径未改变。

## 协议变化

```text
原协议：
  SQ4 coarse codes（固定 4 bit/symbol）
  PQ24 residual codes（固定 6 bit/symbol）

新协议：
  signed root commits canonical Huffman code-length tables
  server 按规范顺序编码 coarse codes 和 selected residual slices
  verifier 解码出完全相同的 symbols
  verifier 重建原 coarse/residual Merkle roots
  后续 top-160/top-768 与对象验证完全不变
```

每个 subspace/coordinate 使用独立 canonical code。code lengths 均不超过 15，两个 4-bit lengths 打包为 1 byte；代码有范围断言。selected residual groups 分别按 byte 对齐，账本已包含 padding，另计 16-byte stream framing。

最终逐项：

| 分项 | 固定码 bytes | Huffman bytes |
|---|---:|---:|
| 公共 routing/leaf 参数 | 50,656 | 50,656 |
| Huffman code-length models | 0 | 2,048 |
| all coarse codes | 65,536 | 49,425.750 |
| selected residual codes | 283,253.232 | 274,024.700 |
| group/fine metadata | 13,952 | 13,952 |
| routing multiproofs | 46,716.512 | 46,716.512 |
| framing | 0 | 16 |
| **routing** | **460,113.744** | **436,838.962** |
| leaf proof | 137,707.142 | 137,707.142 |
| **total** | **597,820.886** | **574,546.104** |

## 三层 residual hierarchy

实现了完整的：

```text
256 coarse bases
    ↓ selected coarse blocks
4,096 middle residual centers
    ↓ selected middle blocks
65,536 fine residual centers/leaves
```

每层的 code blocks、68-byte descriptors 和合并 multiproofs均计费，并加入 coarse/mid/fine 三级 trace-aware 物理布局。

严格 1,000-query 关键点：

| coarse/mid/fine probes | Recall | total VO | 判断 |
|---|---:|---:|---|
| 64/448/768 | 0.98908 | 567.83 KiB（布局前） | recall 不合格 |
| 64/480/800 | 0.99032 | 584.61 KiB | 合格但更大 |
| 64/480/796 | 0.99025 | 583.810 KiB | 与两层逐字节打平、recall 更低 |
| 64/480/792 | 0.99018 | 583.02 KiB | VO 略低但低于 0.99022 门槛 |

最佳合格三层点为 597,821.320 bytes，两层为 597,820.886 bytes，只差 0.434 bytes/query；但三层 Recall 低 0.00048、ANN query 更慢，因此淘汰。

原因：第三层确实减少了 residual-code披露，但新增 8 KiB codebook、middle descriptors/proofs，以及略多的 leaf访问，恰好抵消节省。

## 其他失败方向

- **1-byte residual norm correction：** coarse/mid 解码误差会改变 residual query，孤立修正相对精确父中心的范数反而破坏误差抵消；100-query recall 从 0.9928 降至约 0.9903–0.9925。
- **rANS/范围编码估计：** 2-byte frequency tables 共 8,192 bytes；尽管 payload 更接近熵，总 VO 约 564.91 KiB，反而不如小模型 Huffman 的 561.08 KiB。
- **继续加层：** 依据三层逐字节打平的结果，在相同独立 codebook/descriptor 设计下，第四层大概率继续增加参数和认证开销，不应优先。

## 结果边界和下一步

Huffman payload 是按真实整数 canonical code lengths、逐 group byte padding和模型大小精确计数，不是 Shannon 下界；但原型尚未实际写出/读回 bitstream，也未把 Huffman CPU 计入端到端 verifier latency。下一步应先实现 serializer round-trip 和 root 重建测试。

在保持 stateless、SHA/Merkle 和当前 recall 下，低风险空间已缩到低个位数。剩余重点是：

1. 实际 Huffman serializer/decoder；
2. 少量共享 context classes，而不是每组独立模型；
3. PQ-subspace 对齐的可认证 block omission。

## 实现与结果

- `residual_3level_vo.cpp`：三层残差索引、查询和三级 proof 账本；
- `complete_routing_vo.cpp`：norm correction、entropy/Huffman models；
- `residual_hier_vo.cpp`：两层 Huffman wire-accounting；
- `final_3l_layout_c64_m480_f796_q1000.csv`：三层逐字节打平点；
- `final_2l_huffman4_c160_p768_q1000.csv`：最新最好结果。
