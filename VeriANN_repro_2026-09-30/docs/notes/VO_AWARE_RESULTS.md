# VO-aware SQ Template Selection

配置：SIFT1M、top-k=100。测试 query 为 0–49，训练 query 为 50–99；训练集内部再按 25/25 划分 template selection/validation。候选为全局等宽 SQ2/SQ3/SQ4；每个 IVF cluster 可选择一个固定模板。目标直接最小化 `code + 4-byte epsilon + exact-open vector`。收缩正则由训练内 validation 选择，测试集不参与参数选择。

## 独立测试结果

| nprobe | Recall@100 | 最佳固定 SQ | 最佳固定 VO 减少 | 训练所得模板数 SQ2/3/4 | VO-aware VO 减少 | 相对最佳固定 |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 46.22% | SQ4 | 80.26% | 0 / 0 / 100 | 80.26% | 0.00 pp |
| 2 | 64.04% | SQ4 | 82.87% | 0 / 0 / 100 | 82.87% | 0.00 pp |
| 4 | 85.16% | SQ4 | 84.44% | 0 / 0 / 100 | 84.44% | 0.00 pp |
| 8 | 96.10% | SQ4 | 85.41% | 0 / 0 / 100 | 85.41% | 0.00 pp |
| 16 | 99.58% | SQ3 | 86.02% | 0 / 100 / 0 | 86.02% | 0.00 pp |
| 32 | 100.00% | SQ3 | 87.78% | 0 / 100 / 0 | 87.78% | 0.00 pp |
| 64 | 100.00% | SQ3 | 88.73% | 22 / 78 / 0 | 88.47% | −0.26 pp |
| 100 | 100.00% | SQ3 | 89.16% | 75 / 25 / 0 | 88.57% | −0.59 pp |

## 判断

1. **有效的简单 VO-aware policy 是按 nprobe 选择全局位宽**：nprobe≤8 使用 SQ4，nprobe≥16 使用 SQ3。相对始终使用 SQ4，nprobe=32/64/100 分别多减少 1.43/2.21/2.56 个百分点。
2. **当前 per-cluster workload-aware selection 没有独立测试收益**。低中 nprobe 的训练内验证正确地完全退回全局模板；高 nprobe 的混合 SQ2/SQ3 在测试集发生 workload shift，略差于固定 SQ3。
3. 这不否定 VO-aware objective，但说明仅 50 条训练 query、每簇离散选择 2/3/4 bit 的自由度仍会过拟合。进一步研究需要更多训练 workload、共享少量模板/更强收缩，或优化更稳定的 per-cluster normalization 和 cell bound，而不是报告训练集结果。

统计口径与原量化消融一致，暂不含 ID 和 block authentication。完整结果见 `vo_aware_quantization_results_at100.csv`，每簇选择见 `vo_aware_cluster_templates_at100.csv`。

