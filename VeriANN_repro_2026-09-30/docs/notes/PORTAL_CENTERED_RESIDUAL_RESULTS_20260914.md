# Portal-centered residual block：关键实验结果

日期：2026-09-14  
数据：SIFT100K，HNSW M=16 / efConstruction=100。新方案先以真实 HNSW member 作为 block portal，再以 portal 的认证量化重构值为原点编码 block 内对象 residual，验证顺序为 R4 → R6 → raw。

## 实现

- Portal 选择：优先 HNSW level 更高的 member；同 level 时选更接近原 block centroid 的 member。
- Portal 表示：支持 SQ6、SQ8；Merkle leaf 和 routing payload 按实际 bit 数计费。
- Residual：对对象编码 x - decode(portal)，因此客户端用 decode(portal) + raw residual 可精确重构 x，不依赖未披露的 raw portal。
- Result-driven verification：先由候选对象得到最终 top-k 阈值 tau，再依次使用 R4/R6 下界；只有下界不能证明大于 tau 时才进入下一层或 raw。
- 两种图：
  - contracted-union：合并 block 内所有 member 的跨 block 边；
  - portal-only layered：只保留所选 portal 自身各 HNSW level 的边，并按层执行 greedy descent + level-0 bounded search。
- 安全检查：所有运行的 lower-bound violations 均为 0。

## 20-query pilot

| graph | block | portal | degree | expansion | Recall@10 | expanded blocks | disclosed blocks | candidate blocks | candidate objects | raw | total VO |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| contracted-union | 2 | SQ6 | 32 | 40 | 0.925 | 40.00 | 873.10 | 276 | 552 | 44.35 | 323.00 KiB |
| contracted-union | 2 | SQ8 | 32 | 40 | 0.925 | 40.00 | 875.45 | 276 | 552 | 45.70 | 351.57 KiB |
| contracted-union | 4 | SQ6 | 32 | 40 | 0.795 | 40.00 | 807.05 | 276 | 1104 | 50.25 | 359.18 KiB |
| contracted-union | 4 | SQ8 | 32 | 40 | 0.785 | 40.00 | 808.70 | 276 | 1104 | 50.45 | 384.84 KiB |
| portal-only layered | 2 | SQ6 | 32 | 40 | 0.935 | 43.60 | 615.85 | 276 | 552 | 42.25 | 259.26 KiB |
| portal-only layered | 2 | SQ8 | 32 | 40 | 0.925 | 43.65 | 618.65 | 276 | 552 | 42.80 | 279.72 KiB |
| portal-only layered, absolute object SQ control | 2 | SQ6 | 32 | 40 | 0.935 | 43.60 | 615.85 | 276 | 552 | 27.00 | 242.85 KiB |

现有最佳 centroid/block 方案的 100-query 参考值为 Recall 0.990、expanded 52、disclosed 939.70、candidate blocks 276、candidate objects 552、raw 22.69、总 VO 281.76 KiB。由于本表是 20-query pilot，只用于筛除明显无希望的结构，不把其 Recall 小数点差异当作正式统计提升。

## 结论

Portal-only layered 相比 contracted-union 确实修复了一部分“边与打分语义不一致”：SQ6 下 disclosed 从 873.10 降至 615.85，总 VO 从 323.00 降至 259.26 KiB，Recall 从 0.925 升至 0.935。但它距离约 0.99 Recall 仍很远，因此没有继续做 100-query 大 sweep。

SQ8 相对 SQ6 没有导航收益，却增加约 20 KiB/query，说明当前误差主要不是 portal 量化精度，而是单 portal 无法覆盖 block 内两个或四个 member 的不同邻域。

当前 residual 设计也没有降低对象验证成本。同一条 portal-only SQ6 trace 下，R4/R6 residual 需要 42.25 个 raw，而 absolute SQ4/SQ6 control 只需 27.00 个；总 VO 因而从 242.85 增至 259.26 KiB。原因是一个全局 residual quantizer 被少量跨度大的 block 拉宽，R4/R6 interval 反而比绝对坐标更松。block=4 又把 candidate objects 翻倍到 1104，使结果进一步恶化。

因此本轮结论是：**“单高精度 portal + 全局低比特 residual”不值得继续调 portal bits 或 expansion。** 若继续该方向，最小必要改动应是每 block（或 portal group）独立 residual scale，并允许 2 个 portal 覆盖不同 member 邻域；否则提高 portal 精度无法解决单 portal 的拓扑覆盖损失。

