# Per-object SQ4 + Block-level Authenticated Posting Lists

配置：SIFT1M、IVF100、前 50 条 query、top-k=100。验证记录为 `ID(4B) + packed SQ4 code(64B) + per-object epsilon(4B) = 72B`。

## 认证结构

每个 IVF posting list 按连续位置分块。每个块分别提交：

- `quant_root = SHA256(all ID/code/epsilon records)`；
- `exact_root = MerkleRoot(H(ID || exact vector))`；
- `block_leaf = SHA256(quant_root || exact_root || cluster_id || block_id || count)`。

块叶组成 cluster Merkle root，100 个 cluster roots 再组成全局 root，由数据拥有者签名。查询访问整个 posting list，因此客户端直接重算所有量化块；只对少量 exact-open 对象提供块内 exact multiproof，再用 probed-cluster multiproof 重建全局根。实现使用 OpenSSL SHA-256，并执行了篡改自检。

## block=256 的核心结果

| nprobe | Recall@100 | 平均扫描 | 平均原向量打开 | Block auth/query | 朴素对象认证/query | 认证减少 | 完整 VO vs 原始减少 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 46.22% | 11,789.6 | 760.92 | 70.1 KB | 352.9 KB | 80.13% | 78.49% |
| 2 | 64.04% | 23,052.4 | 886.76 | 96.8 KB | 709.3 KB | 86.35% | 81.42% |
| 4 | 85.16% | 45,223.6 | 1,030.42 | 130.3 KB | 1,414.2 KB | 90.79% | 83.23% |
| 8 | 96.10% | 87,655.8 | 1,143.82 | 161.9 KB | 2,768.4 KB | 94.15% | 84.39% |
| 16 | 99.58% | 170,909.1 | 1,209.34 | 190.0 KB | 5,430.4 KB | 96.50% | 85.13% |
| 32 | 100.00% | 330,846.5 | 1,224.08 | 218.9 KB | 10,547.9 KB | 97.92% | 85.55% |
| 64 | 100.00% | 616,012.6 | 1,224.78 | 264.0 KB | 19,673.2 KB | 98.66% | 85.77% |
| 100 | 100.00% | 1,000,000.0 | 1,224.78 | 323.9 KB | 31,960.8 KB | 98.99% | 85.86% |

“朴素对象认证”是假设量化对象为了绑定隐藏的原向量，需要分别携带一个 32-byte `H(x)`；因为完整 probed lists 都被返回，两种方案都已使用共享 Merkle reconstruction，而没有重复计算独立路径。block 方案用每块 exact root 和块内 multiproof 取代逐对象 `H(x)`。

在 nprobe=8 时，block=256 将最终完整 VO 从朴素认证约 9.67 MB/query 降到 7.06 MB/query，相对原始 45.23 MB/query 减少 84.39%；朴素认证只能减少约 78.63%，所以批量认证贡献约 5.76 个百分点。在 nprobe=100 时贡献约 6.13 个百分点。

## Block size 消融

| Block size | nprobe=8 认证减少 | nprobe=8 总 VO 减少 | nprobe=100 认证减少 | nprobe=100 总 VO 减少 |
|---:|---:|---:|---:|---:|
| 64 | 93.36% | 84.35% | 97.60% | 85.78% |
| 128 | 93.93% | 84.38% | 98.53% | 85.83% |
| 256 | 94.15% | 84.39% | 98.99% | 85.86% |
| 512 | 94.25% | 84.40% | 99.21% | 85.88% |
| 1024 | 94.28% | 84.40% | 99.32% | 85.88% |

512/1024 的额外收益已经很小。256 是较合理的折中：认证字节已下降 94%–99%，同时块不会过大，适合流式验证、缓存和局部更新。

完整数据见 `block_auth_results_at100.csv`。VO 口径包含 ID、SQ4 code、float epsilon、exact-open vector、块 header、块内 exact multiproof 和全局 cluster multiproof；不包含固定大小的根签名与查询结果签名。

