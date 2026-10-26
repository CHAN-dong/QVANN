# 源码分类索引

为避免破坏大量 `#include "other_experiment.cpp"` 的研究原型依赖，48 个实验源文件保持原来的扁平目录；本文件提供逻辑分类。

## 最终主线：Relaxed HNSW、progressive quantization 与认证

- `relaxed_hnsw_harvest.cpp`：固定 beam/round 的早期 Expand-or-Harvest。
- `e_budget_relaxed_hnsw.cpp`：统一为单参数 expansion budget E；最终 SQ4 top-1 trace 来源。
- `progressive_relaxed_hnsw.cpp`：SQ4→Delta4→6 的渐进式 payload。
- `aggregated_mac_hnsw.cpp`：聚合 MAC 对照。
- `basic_hnsw_vo_breakdown.cpp`、`offline_basic_vo_breakdown.cpp`、`hnsw_vo_breakdown.cpp`：Basic/quantized/relaxed HNSW VO 拆分。
- `expansion_centric_merkle.cpp`：Record-wise、带 neighbor hash 的 ER、Direct-content ER。
- `refinement_merkle_ordering.cpp`：Node-ID、HNSW BFS、SQ4-code refinement ordering。

## Routing graph / block / adjacency

- `hnsw_block_graph.cpp`：block-aware HNSW、bounded cross-block edges 和 portal/residual 系列。
- `region_block_graph.cpp`、`region_block_graph_local.cpp`：region/block graph 原型。
- `verification_block_screen.cpp`：verification-aware block summary 离线筛选。
- `sq4_progressive_adjacency.cpp`：Primary/Residual adjacency 和 deterministic fallback。
- `global_centroid_hnsw_vo.cpp`：global centroid HNSW routing。
- `microcluster_routing.cpp`：microcluster routing。
- `progressive_routing_vo.cpp`：routing payload/proof 分阶段统计。

## IVF、批处理和量化

- `main.cpp`：早期基础 VeriANN/IVF 入口。
- `quantization.cpp`、`vo_aware_quantization.cpp`、`vo_latency_quantization.cpp`：量化和 VO/latency 权衡。
- `progressive_sq.cpp`、`progressive_sq_fast.cpp`：IVF progressive SQ。
- `ivf_granularity.cpp`：IVF leaf granularity。
- `trace_aware_merkle_ivf.cpp`：trace-aware Merkle IVF。
- `workload_block_sq.cpp`、`workload_hybrid_vo.cpp`：早期 workload-aware 设计。
- `block_auth.cpp`、`batch_pruning.cpp`、`adaptive_batch.cpp`、`query_aware_batch.cpp`、`pivot_batch.cpp`、`box_batch.cpp`：block authentication 与 batch pruning 系列。

## Residual/PQ 与完整 VO tree

- `residual_pq.cpp`、`residual_hier_vo.cpp`、`residual_3level_vo.cpp`：residual quantization 和多层 VO。
- `pq_block_tree_vo.cpp`、`pq_graph_vo.cpp`、`pq_search_tree_vo.cpp`：PQ block/graph/search tree。
- `complete_routing_vo.cpp`、`complete_block_vo.cpp`：完整 routing/block VO 计费。
- `best_first_auth_tree.cpp`、`adaptive_auth_tree.cpp`、`auto_vo_tree.cpp`：认证树搜索与自动结构选择。
- `budgeted_residual_vo.cpp`、`compact_probe_allocator.cpp`：预算化 residual 和 probe 分配。
- `joint_vo_successive_refinement.cpp`、`joint_vo_bound_pruning.cpp`：联合结构/探测优化与下界剪枝。

## 阅读顺序

若只关心最终论文主线，建议按以下顺序阅读：

1. `src/verifiable_ann_common.hpp`
2. `relaxed_hnsw_harvest.cpp`
3. `e_budget_relaxed_hnsw.cpp`
4. `expansion_centric_merkle.cpp`
5. `refinement_merkle_ordering.cpp`
6. `docs/CODE_WALKTHROUGH_CN.md`

