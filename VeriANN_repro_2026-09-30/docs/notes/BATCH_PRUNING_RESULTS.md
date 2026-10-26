# IVF 已知阈值下的认证批量排除实验

## 方法

在每个 IVF posting list 内构建方差维二分 Ball Tree。每个树节点保存：

- SQ4 压缩中心 `c_hat`；
- 中心量化误差 `epsilon_c`；
- 覆盖半径 `radius`；
- 子树数量；
- 左右子树 commitment。

使用安全下界：

```text
LB(q,node) = max(0, ||q-c_hat|| - epsilon_c - radius)
```

在已知精确 top-k 阈值 `tau` 后，如果 `LB > tau`，整个子树被一个认证摘要替代；否则继续向下。不能批量排除的 leaf 才发送现有的 72-byte `ID + SQ4 + epsilon + exact hash` 对象记录，区间仍重叠的对象再发送 512-byte 原向量。

压缩节点摘要按 leaf 108 bytes、internal 140 bytes 计费，已经包含中心证书和子树哈希。`flat_sq4_bytes` 与 `batch_sq4center_bytes` 都不含固定 root 签名；树节点 commitment 取代 flat 方案对应的认证路径。`batch_exactcenter_bytes` 是在相同剪枝决策下，把中心摘要替换成 float32 中心的开销消融，不代表重新使用更紧 exact-center bound 搜索。

## SIFT100K，50 queries，k=100

最优 leaf size 下的结果：

| nprobe | 最优 leaf | 批量排除比例 | VO 变化 |
|---:|---:|---:|---:|
| 8 | 256 | 0.00% | **+1.33%** |
| 16 | 256 | 0.00% | **+1.55%** |
| 32 | 256 | 0.00% | **+1.69%** |
| 64 | 256 | 0.16% | **+1.59%** |
| 100 | 64 | 17.40% | **−9.81%** |

常用的中低 nprobe 下，Ball Tree 下界不能排除 batch，树摘要成为纯额外开销。只有扫描全部 100 个 IVF lists 时，top-k 阈值足够紧，批量排除开始产生净收益。

## SIFT1M，10 queries，k=100

| nprobe | leaf | 批量排除比例 | VO 变化 |
|---:|---:|---:|---:|
| 32 | 256 | 0.00% | **+1.91%** |
| 64 | 256 | 1.03% | **+0.83%** |
| 100 | 32 | 34.57% | **−22.80%** |
| 100 | 64 | 28.22% | **−21.82%** |
| 100 | 128 | 22.78% | **−19.32%** |
| 100 | 256 | 17.94% | **−16.08%** |

负号表示 VO 降低。SIFT1M 的结论与 100K 一致：全扫描时有效，正常 `nprobe=32/64` 时无效。

## 判断

批量排除有条件地有用，而不是通用替代现有 block authentication：

- `nprobe=100`：值得使用，可进一步减少约 16%–23% 的 SQ4/exact payload；
- `nprobe=64`：基本处于盈亏平衡附近；
- `nprobe<=32`：普通 Ball Tree 不值得启用。

建议使用查询/参数自适应策略：只有预测 `tau` 足够小或 `nprobe` 足够大时启用树；否则直接使用现有 SQ4 block authentication。若要让中等 nprobe 也获益，需要比单球更紧的 batch bound，例如多中心覆盖、VO-aware micro-clustering，或者先用训练 workload 将容易共同排除的对象放在同一 leaf。

## 固定小块逐块排除：block size 2/4/8/16

另行测试了不发送任何内部树节点、直接检查每个几何 leaf block 的方案。每个 block 固定发送一个 108-byte SQ4 center/radius/subtree-root 摘要；只有未排除 block 才发送其中的对象记录。这更直接对应“每块 2、4、8、16 个对象”。

SIFT100K、50 queries：

| nprobe | block=2 | block=4 | block=8 | block=16 |
|---:|---:|---:|---:|---:|
| 8 | +45.16% | +32.86% | +18.81% | +9.92% |
| 16 | +49.60% | +36.94% | +21.66% | +11.50% |
| 32 | +48.34% | +37.80% | +23.01% | +12.38% |
| 64 | +30.72% | +20.08% | +11.44% | +6.17% |
| 100 | +15.06% | **−3.50%** | **−13.19%** | **−16.25%** |

SIFT1M、10 queries、`nprobe=100`：

| block size | 对象排除率 | 净 VO 变化 |
|---:|---:|---:|
| 2 | 77.67% | +9.60% |
| 4 | 60.25% | **−12.41%** |
| 8 | 49.91% | **−24.47%** |
| 16 | 41.67% | **−28.37%** |

这里 `+` 表示 VO 增大，`−` 表示 VO 降低。block=2 虽然排除率最高，但每两个对象都要付一个 108-byte 摘要，认证粒度过细；block=8/16 的摘要摊销更好，反而获得最大净收益。完整数据见 `batch_pruning_small_blocks_sift100k_k100.csv` 和 `batch_pruning_small_blocks_sift1m_k100_q10.csv`。

## 半径感知的相似向量自适应合并

固定块实验本身已经使用几何相似分组：posting list 内按最大方差递归二分，而不是随机或 ID 连续分块。进一步测试了可变大小合并规则。只有同时满足以下条件才把左右子块合并：

```text
merged_count <= max_block
merged_radius <= gamma * max(left_radius, right_radius, center_quant_error)
```

否则保留为更小的相似向量块。`gamma` 越小，越严格限制半径膨胀。

SIFT100K、`max_block=16`：

| gamma | 平均块大小 | nprobe=64 VO变化 | nprobe=100 VO变化 |
|---:|---:|---:|---:|
| 1.00 | 1.09 | +43.11% | +41.98% |
| 1.05 | 1.46 | +31.86% | +24.96% |
| 1.10 | 2.79 | +17.27% | +2.24% |
| 1.15 | 6.03 | +9.02% | **−11.36%** |
| 1.20 | 9.23 | +6.74% | **−15.25%** |
| 1.30 | 10.80 | +6.18% | **−16.22%** |
| 固定 max-16 | 10.87 | +6.17% | **−16.25%** |

结果支持“必须按相似性分组”，但当前半径阈值没有超过固定几何 block=16：严格限制半径会产生过多 108-byte block 摘要；放宽到有净收益时，分组已经几乎退化为固定 max-16。`max_block=32` 的最佳结果为降低约 15.49%，也没有超过 max-16。

这说明优化目标不能只限制半径增长，而应直接最小化预期 VO：

```text
expected_cost(block) = 108B + P(not_pruned) * object_payload(block)
```

只有当合并后摘要摊销收益大于下界变松造成的打开概率增长时才合并。完整结果见 `adaptive_batch_results_sift100k_k100.csv` 和 `adaptive_batch_results_sift100k_k100_max32.csv`。

完整数据见 `batch_pruning_results_sift100k_k100.csv` 和 `batch_pruning_results_sift1m_k100_q10.csv`。
