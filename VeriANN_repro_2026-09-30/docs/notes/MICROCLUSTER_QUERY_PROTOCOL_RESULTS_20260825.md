# Verifiable ANN：认证微簇索引与两级查询协议实验

## 结论先行

你的判断是对的：继续训练普通量化已经不是主方向，真正有效的是改变“哪些对象进入证明域”。本轮最合适的方案是：

1. 把 IVF 叶子细化成接近 singleton 的认证微簇；
2. 把正确性定义为“忠实执行公开、确定性的 ANN 查询协议”，而不是逐对象证明所有未命中向量都在阈值外；
3. 用两级公开 centroid 路由恢复查询效率；
4. 对长期 verifier 缓存认证 leaf directory，进一步消除每查询 routing multiproof。

在 SIFT100K、`k=100`、200 个官方 learn query 训练、100 个独立 test query 上：

- 旧最强结果：Recall@100 = 0.9939，VO = 416.64 KiB/query；
- 新的两级冷启动方案：Recall@100 = 0.9942，VO = 124.15 KiB/query，安全违规为 0；
- VO 相对旧结果下降 70.20%，访问对象数从 4,979.03 降到 714.69，下降 85.65%；
- 完整两级协议在 Recall@100 = 0.9942 时为 9.81 ms/query，低于原配置实测的 11.24 ms/query；
- 若 verifier 缓存 2.25 MiB 的 leaf directory，稳态 VO 为 77.79 KiB/query；约 50 次查询后摊销优于每次发送冷 routing proof。

所以方向上的优先级很清楚：**索引粒度与查询协议 > routing commitment/cache > 量化细调 > 大块几何摘要**。

## 1. 为什么这次结构改动有效

旧协议先用 IVF 选中较大的 posting lists，然后对 list 中每个 visited object 至少发送一个 base certificate。即使绝大多数对象显然不可能进入 top-k，它们仍然各占一个记录。之前 416.64 KiB 方案中，base payload 占 67.7%。

新的协议不再试图用一个高维 Ball/Box 证明“大组对象全部不可能命中”。它把 list 细化为认证微簇，并把以下过程绑定进签名索引版本和查询参数：

```text
query q
  -> 公开、确定性的 coarse centroid 选择
  -> 在选中 coarse groups 内确定性选择 fine microclusters
  -> 只打开这些 fine leaves
  -> leaf 内继续使用安全的 nested SQ + lossless tail 验证
```

未选择的微簇不是被声称为“精确距离必然大于 tau”，而是按公开 ANN 协议合法地不进入搜索域。Verifier 保证的是：服务器没有篡改索引、没有偷偷换 leaf、没有少返回协议要求访问的对象，并正确执行了 leaf 内证书；Recall 则像普通 ANN 一样由独立测试集统计保证。

这正是允许改变查询协议后最大的自由度：从“证明近似结果接近全局精确 top-k”改成“证明服务器忠实执行一个召回率经过校准的 ANN 算法”。

## 2. 实验设置与纪律

- 数据：SIFT base 前 100,000 个 128-D 向量；
- 查询：前 200 个 `sift_learn` 只用于训练/选择，100 个 `sift_query` 只用于最终报告；
- 任务：Recall@100，精确 ground truth 在 SIFT100K 子集上重新计算；
- 索引：确定性 hierarchical k-means，6 次 split refinement；
- 叶内协议：workload-aware nested 3/4-bit base -> SQ6 refinement -> lossless integer tail；
- 认证：SHA-256 Merkle multiproof 模型、position-addressed ID、public dummy hash；
- 路由布局：trace layout 只在一半 learn query 上拟合，另一半在 identity、geometry、trace 间选择，避免 fine IVF 下的 trace 过拟合；
- 安全性：所有量化 lower bound 做 exact-distance 检查，所有报告点违规数均为 0；
- 时间：VO 生成重复 3 次；两级路由实验独立重复 3 次并检查 checksum 确定性。

这里没有把 fine centroids 的静态传输计入单查询 VO；这与原实验“静态 centroids 已缓存”的假设相同。后文会单独列出新增 directory cache 的摊销成本。

## 3. 索引粒度与 VO–Recall 前沿

| nlist | nprobe | Recall@100 | visited/query | 冷 VO/query | VO 生成 |
|---:|---:|---:|---:|---:|---:|
| 16,384 | 640 | 0.9939 | 4,979.03 | 418.90 KiB | 4.240 ms |
| 32,768 | 640 | 0.9918 | 2,417.72 | 263.64 KiB | 2.379 ms |
| 32,768 | 768 | 0.9953 | 2,882.61 | 304.91 KiB | 2.896 ms |
| 65,536 | 256 | 0.9828 | 443.27 | 81.69 KiB | 0.749 ms |
| 65,536 | 384 | 0.9930 | 660.29 | 115.89 KiB | 0.970 ms |
| 65,536（flat route） | 416 | 0.9944 | 714.59 | 124.16 KiB | 0.953 ms |
| **65,536（two-stage）** | **416** | **0.9942** | **714.69** | **124.15 KiB** | **1.234 ms** |
| 65,536 | 512 | 0.9971 | 877.27 | 148.27 KiB | 1.231 ms |

16,384-list 严格重跑采用新的 held-out layout selector，因此是 418.90 KiB；此前最终报告的 416.64 KiB 使用原选择流程。完整两级方案相对二者分别下降 70.36% 和 70.20%，结论不受这 0.54% 差异影响。

这个前沿还提供了灵活协议点：如果 0.983 recall 可接受，81.69 KiB 已比旧方案低 80.40%；如果要求约 0.997 recall，148.27 KiB 仍低 64% 以上。

原始结果：

- `workload_microcluster_baseline_ivf16384_np640_20260825.csv.protocol.csv`
- `workload_microcluster_ivf32768_np640_20260825.csv.protocol.csv`
- `workload_microcluster_ivf32768_np768_20260825.csv.protocol.csv`
- `workload_microcluster_frontier_ivf65536_np256_20260825.csv.protocol.csv`
- `workload_microcluster_frontier_ivf65536_np384_20260825.csv.protocol.csv`
- `workload_microcluster_final_ivf65536_np416_20260825.csv.protocol.csv`
- `workload_microcluster_twostage_final_ivf65536_np416_c1024_cp224_20260826.csv.protocol.csv`
- `workload_microcluster_frontier_ivf65536_np512_20260825.csv.protocol.csv`

## 4. “批量证明明显不可能对象”仍然值得做吗

在匹配点 `nlist=65536, nprobe=416` 上，我再次测试了 list-level Ball、4/6-bit Box 和训练选择摘要。为了公平，下面都与同一 trace-VT leaf 协议比较：

| list summary | pruned vectors/query | VO/query | 相对不加 summary |
|---|---:|---:|---:|
| 不加 summary | 0 | 216.92 KiB | 0 |
| 全部 Ball | 18.66 | 242.18 KiB | +11.65% |
| 全部 Box4 | 23.53 | 266.08 KiB | +22.66% |
| 全部 Box6 | 107.87 | 281.68 KiB | +29.85% |
| train-selected | 0.04 | 216.93 KiB | 约 0% |

结论仍然是否定的。高维 overlap 使安全包围边界太松，摘要本身的固定成本又高。即使 Box6 排除了 107.87 个对象，节省的 leaf payload 和 Merkle 节点也覆盖不了摘要。训练选择器最后只启用 65,536 个 list 中的 2 个，等价于主动放弃该方向。

这说明“批量排除”概念没有错，但合适的实现不是在较大 posting list 上再套全维 Ball/Box；应当直接让聚类/微簇成为查询协议的排除单位。

## 5. 两级确定性路由：把查询效率拿回来

65,536 个 fine centroids 全扫描会增加路由时间。新实验把它们静态聚成 1,024 个 coarse groups；查询先选 coarse groups，再只比较组内 fine centroids。两层 centroids 和 fine-to-coarse mapping 都属于公开、认证的索引元数据，选择过程使用固定 tie-break，因此服务器不能操纵 shortlist。

固定 fine `nprobe=416`：

| 路由 | coarse nprobe | fine centroid comparisons/query | Recall@100 | visited/query | 总查询时间 |
|---|---:|---:|---:|---:|---:|
| flat fine scan | 1,024（全部组） | 65,536 | 0.9944 | 714.59 | 19.88 ms |
| two-stage | 64 | 4,900.82 | 0.9605 | 712.44 | 5.11 ms |
| two-stage | 128 | 9,575.37 | 0.9894 | 714.43 | 7.18 ms |
| two-stage | 192 | 13,994.36 | 0.9938 | 714.74 | 9.30 ms |
| **two-stage** | **224** | **16,125.21** | **0.9942** | **714.69** | **10.56 ms** |
| two-stage | 256 | 18,229.17 | 0.9943 | 714.66 | 10.82 ms |

独立 routing microbenchmark 中，选择 224 个 coarse groups 时相对 flat fine scan 加速 46.91%，Recall 只下降 0.0002。接入完整 VO evaluator 后，同一两级 leaf 集合实测 Recall=0.9942、VO=124.15 KiB、查询 9.81 ms；相对原 `16384x640` 的 11.24 ms 加速 12.67%。因此新的结构和协议没有用查询效率换 VO。

原始结果：`microcluster_routing_sift100k_k100.csv`、`microcluster_routing_sift100k_k100_dense.csv`。实现：`microcluster_routing.cpp`。

## 6. 匹配点的 VO 组成

`65536x416 + coarse 1024x224` 最终冷 VO 为 127,126.60 B/query：

| 组成 | B/query | KiB/query | 占比 |
|---|---:|---:|---:|
| 488-bit base code | 43,596.09 | 42.57 | 34.29% |
| SQ6 refinement | 16,252.25 | 15.87 | 12.78% |
| lossless tail + ID | 2,611.38 | 2.55 | 2.05% |
| local multiproofs | 17,196.48 | 16.79 | 13.53% |
| routing multiproof | 47,470.40 | 46.36 | 37.34% |
| **合计** | **127,126.60** | **124.15** | **100%** |

微簇把 base payload 从旧方案的 282.02 KiB 降到了 42.57 KiB；瓶颈因此转移到了 routing。完整两级协议的 VO 生成从 4.240 ms 降到 1.234 ms，下降 70.91%。

## 7. 冷/暖 verifier 协议

对静态索引，可以发布一个由索引根签名的 leaf directory：每个 fine leaf 缓存 32 B root 和保守按 4 B 计算的 length，共 `65536 * 36 B = 2.25 MiB`。Fine centroids 已属于原实验的公共缓存，不重复计算。

Verifier 有 directory 后可以直接核对 leaf root 和长度，确定性的路由又能自行得到 leaf IDs，因此每查询 47,470.40 B 的 routing 部分可省掉。Local refinement/exact proofs仍保留。

| 同一索引上的查询数 | 含 2.25 MiB 一次性 cache 的摊销 VO/query |
|---:|---:|
| 10 | 308.19 KiB |
| 50 | 123.87 KiB |
| 100 | 100.83 KiB |
| 1,000 | 80.09 KiB |
| 稳态极限 | 77.79 KiB |

Break-even 是 49.70 次查询。少量一次性查询用 124.15 KiB 冷协议；长会话或服务型 verifier 用 directory 协议。这个结果是按实测 VO 组成做的精确字节摊销模型，不是网络端到端实测，需要在实际系统中再测 cache miss、索引更新和并发版本管理。

## 8. 安全边界

必须清楚地区分两个保证：

- 密码学/数值安全保证：返回记录属于签名索引；选中 leaves 与公开确定性协议一致；nested SQ lower bound 保守；需要打开的 refinement/tail 没被服务器省略。本实验 violations = 0。
- ANN 质量保证：0.9942 是独立 query 上的统计 Recall@100，不是“每个 query 都至少 0.9942”的密码学定理。

协议必须把 `index_version, coarse_nlist, coarse_nprobe, fine_nlist, fine_nprobe, k, distance_metric, tie_break` 全部绑定。Directory cache 必须绑定同一 index root；索引更新后旧 cache 失效。若应用要求证明全局 exact top-k，则不能合法省掉未访问微簇，这个方案不适用于该更强语义。

## 9. 哪些方向更合适，哪里还有空间

### 第一优先：层级认证微簇与 routing commitment 联合设计

这是已经被数据验证的主方向。下一步不应只把当前 flat leaf Merkle 做排列，而应让 1,024 coarse groups 与 fine leaves 在认证树中物理连续：coarse root -> child fine roots -> leaf records。这样 two-stage 查询天然打开较少连续子树，有机会在不缓存完整 directory 时继续压缩 46.38 KiB routing。

### 第二优先：按 query 难度自适应 probe budget

固定 `nprobe=416` 对 easy query 是浪费。可在 learn query 上用 centroid distance gap、coarse shortlist stability 和历史 recall 训练低自由度难度分桶，为每桶选择 `{256,320,384,416,512}`。必须在独立 validation 上选择安全 margin，再只在 test 上报告。这个方向会同时降低访问对象、VO 和查询时间，预期比继续调每维量化边界更有价值。

### 第三优先：冷/暖双协议和 proof cache

本轮 full directory 已给出 77.79 KiB 稳态上界。实际系统可做三档：root-only 冷证明、热点 Merkle-node cache、full leaf directory。热点 cache 应只用 learn/workload trace 选节点，并在索引版本更新时原子失效。

### 第四优先：更强的 routing commitment

若允许改变密码学工具，可测试 Verkle/vector commitment 或按 coarse group 的聚合 opening，目标是让数百个 fine-root opening 不再线性携带大量 sibling hashes。要同时报告 prover time、verifier time、setup/曲线假设，不能只报 proof bytes。

### 不建议继续投入的方向

- 大 posting list 上的全维 Ball/Box；
- per-object 量化 bit 的高自由度训练；
- exact tail 的继续微调：当前只占 2.06%；
- 只优化 Merkle 排列但不改变 leaf 粒度/层级拓扑。

### 剩余空间判断

- 冷协议中 routing 占 37.34%，所以只改 routing 的理论上限是从 124.15 降到 77.79 KiB；现实中 cache 或新 commitment 获取 20%–40% 的进一步冷 VO 降幅是可信目标。
- 暖协议中 base + refinement 占 75% 左右，之后需要自适应 nprobe、层级 leaf grouping 或新的低成本批量证书；继续普通量化很难再给大幅收益。
- 把 `nlist` 从 65,536 继续推到接近 100,000 还有实验空间，但会趋向 singleton/exact centroid scan，增加元数据、泄露风险和路由计算；应与 two-stage/PQ centroid routing 联合扫描，而不是单独追求更细。
- 在当前 Merkle + 统计 ANN 语义下，仍有约 2 倍级别的现实空间；要再做数量级下降，大概率需要更强的向量承诺、证明电路，或者接受更低 recall/更强的客户端缓存。

## 10. 复现

```bash
cd VeriANN
make workload_hybrid_vo_experiment microcluster_routing_experiment

./workload_hybrid_vo_experiment \
  --dataset ../dataset/sift --base 100000 \
  --train-queries 200 --test-queries 100 --k 100 \
  --nlist 65536 --nprobe 416 \
  --coarse-nlist 1024 --coarse-nprobe 224 \
  --iters 6 --repeats 3 \
  --output workload_microcluster_twostage_final_ivf65536_np416_c1024_cp224_20260826.csv

./microcluster_routing_experiment \
  --dataset ../dataset/sift --base 100000 --queries 100 --k 100 \
  --fine 65536 --probe 416 --coarse 1024 \
  --coarse-probes 16,32,64,128,160,192,224,256 \
  --iters 6 --repeats 3 \
  --output microcluster_routing_sift100k_k100.csv
```
