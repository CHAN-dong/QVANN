# VeriANN 核心代码思路与逐行导读

本文不复制大段源码，只用“文件:行号 → 作用”说明最终研究路径。行号以本复现包快照为准。

## 1. 总体数据流

查询先在原始 HNSW topology 上运行 SQ4 top-1 traversal，并用唯一参数 `E=64` 限制 expanded nodes。所有 discovered nodes 进入候选集合；服务器精确计算候选 top-k 和最终阈值 τ。客户端随后用 SQ4 lower bound 排除对象，无法排除者补 Delta4→6，再无法排除者打开 raw vector。认证层不改变 trace：routing 使用 Direct-content Expansion Record，refinement 使用独立 Delta/Raw Merkle trees。

## 2. 公共基础设施：`src/verifiable_ann_common.hpp`

| 行号 | 作用 |
|---|---|
| 1–24 | 引入容器、文件、OpenSSL SHA-256 和 Faiss/HNSW 接口。 |
| 28–52 | 定义 32B `Hash`、SHA-256、左右子哈希和 canonical byte append。 |
| 55–65 | `Matrix` 保存连续 float 向量并提供行访问。 |
| 67–87 | 读取 `.fvecs`，检查维度一致性和文件截断。 |
| 122–126 | `MerkleProof` 保存真实 sibling nodes；proof bytes 只计算 32B hashes。 |
| 128–205 | 构建补齐到 2 的幂的 Merkle tree；`prove()` 去重 targets 并共享路径；`verify()` 重算 root。 |
| 321–356 | `HnswIndex` 包装 Faiss HNSW，提供原向量、ID 映射和完整分层邻接表。 |
| 358–366 | 从缓存文件读取 HNSW，并检查维度和对象数。 |
| 368–405 | 优先复用缓存；缓存不存在时构建并保存 HNSW。 |
| 723–731 | multiproof 正确性和篡改检测：先验证成功，再翻转一个 leaf bit 并要求验证失败。 |

## 3. 单参数 SQ4 traversal：`src/experiments/e_budget_relaxed_hnsw.cpp`

| 行号 | 作用 |
|---|---|
| 1–3 | 嵌入早期 relaxed traversal 的数据加载和 ground-truth 代码，但重命名旧 `main`。 |
| 8–15 | `ESQ` 生成共享 SQ6 code；高 4 bit 是 SQ4，低 2 bit 是 Delta4→6。 |
| 10 | 扫描 SIFT100K 每维 min/max，并将每个维度确定性量化到 6 bit。 |
| 11 | 用量化 bin 中点计算排序距离。 |
| 12–13 | 计算安全 lower/upper bounds，使用 `nextafter` 避免浮点边界不安全。 |
| 17 | `ETrace` 保存 disclosed、expanded、refined、raw、opened adjacency 和各类 bytes/time。 |
| 19 | 初始化一次查询；`seen/expanded/refined` bitmap 保证对象只计一次。 |
| 20 | 首次发现节点时只生成 SQ4 score 和 bounds。 |
| 21 | 需要时补 SQ6，不重复 refinement。 |
| 22 | 定义 SQ4 top-1、top2-overlap 和 certified 三种出队规则；最终方案使用 `sq4`。 |
| 23 | 打开完整 `(node,level)` adjacency，并用 `E` 控制 unique expanded nodes。 |
| 24–25 | 从原 HNSW entry point 完成上层导航，仍计入统一 E budget。 |
| 26–34 | level-0 frontier 按 `(SQ4 distance,node ID)` 排序；每次 top-1 出队并发现邻居。 |
| 35 | 固化 disclosed 集合和 routing 时间。 |
| 36 | 服务器只在 disclosed 集合中用 raw distance 求最终 top-k 和 τ。 |
| 37 | 客户端统一执行 SQ4→SQ6→raw lower-bound 验证，并统计真实 payload/metadata。 |
| 40–43 | 独立实验入口；100 queries，输出 E/mode sweep，核心行是 `sq4,E=64`。 |

## 4. Expansion-Centric Merkle：`src/experiments/expansion_centric_merkle.cpp`

| 行号 | 作用 |
|---|---|
| 1–2 | 复用上面的 `run_e`，保证认证实验不重写搜索轨迹。 |
| 7–14 | `AuthSQ` 将相同 6-bit code打包成 64B SQ4 和 32B Delta。 |
| 16–21 | 给 SQ4、Delta、Raw、Adjacency 定义 domain-separated leaf hash。 |
| 22 | 旧 ER：每个 neighbor 保存额外 32B SQ4 hash commitment。 |
| 23 | Direct-content ER：按 node ID 排序完整邻接表，直接编码 `(neighbor ID,SQ4 bytes)`。 |
| 24 | 客户端只用本查询 SQ4 cache 重构相同 Direct-content ER leaf。 |
| 25 | 对 target positions 排序去重，防止重复认证同一 leaf。 |
| 27 | 聚合 recall、targets、siblings、payload 和 VO。 |
| 31 | 固定 SIFT100K、100 queries，并加载完全相同 HNSW cache。 |
| 32 | 构造 Record-wise、旧 ER 和 Direct-content 三棵真实 SHA-256 trees。 |
| 33 | 初始化逐查询明细 CSV。 |
| 34 | 对每个 query 只运行一次 `run_e(...,64,"sq4")`；三个认证方案复用同一 trace；重构 ER、生成 multiproof、验证 root 和篡改检测、计费。 |
| 35 | 输出三方案均值，包括 proof、commitments、payload 和 total VO。 |

## 5. Refinement ordering：`src/experiments/refinement_merkle_ordering.cpp`

| 行号 | 作用 |
|---|---|
| 1–2 | 复用 Direct-content ER、量化和固定 traversal，关闭被包含文件的旧 `main`。 |
| 6–12 | 一种 ordering 保存 `order`、反向 `position`、Delta tree 和 Raw tree。 |
| 14–27 | 保存每种 ordering 的 target、sibling、proof 和 total VO 累计值。 |
| 29–33 | Node-ID baseline：leaf position 等于对象 ID。 |
| 35–68 | 从 HNSW entry point 做 deterministic level-0 BFS；邻居先按 ID 排序；若非连通则从最小未访问 ID 继续。 |
| 70–83 | 按完整 64B SQ4 byte sequence 做 lexicographic ordering，相同 code 用 node ID tie-break。 |
| 85–98 | Delta/Raw 使用同一 ordering，但构建两棵独立真实 Merkle trees。 |
| 102–113 | 加载固定数据/index，建立 SQ4，并计算精确 ground truth。 |
| 115–132 | 构造保持不变的 Direct-content routing tree。 |
| 134–139 | 生成三种静态 ordering；没有读取 query workload 或历史统计。 |
| 148–152 | 每个 query 只运行一次固定 `run_e(...,64,"sq4")`，所以三种 ordering 的 target IDs 完全相同。 |
| 154–168 | 从 disclosed SQ4 cache 重构 routing ER，并验证 routing multiproof。 |
| 170–183 | 固定 payload，把相同 refined/raw node IDs 映射为三种 leaf positions。 |
| 185–188 | 分别生成并验证 Delta 和 Raw multiproof。 |
| 190–206 | 统计 refinement siblings、factorized whole-query proof 和 total VO。 |
| 208–214 | 写每查询明细，便于检查 target 集合是否一致。 |
| 218–249 | 以 Node-ID 为 baseline，计算 refinement proof 和 total VO reduction。 |
| 250–262 | 打印摘要、BFS 分量数并处理错误。 |

## 6. 最终结果对应关系

| 结果 | 源码 | 参考 CSV |
|---|---|---|
| SQ4 top-1 / E=64 / 76.011 KiB MAC VO | `e_budget_relaxed_hnsw.cpp` | `results/key/e_budget_relaxed_hnsw_q100.csv` |
| Direct-content ER / 132.341 KiB Merkle VO | `expansion_centric_merkle.cpp` | `results/key/expansion_centric_merkle_q100.csv` |
| BFS ordering / 121.255 KiB factorized Merkle VO | `refinement_merkle_ordering.cpp` | `results/key/refinement_merkle_ordering_q100.csv` |

## 7. 阅读其他实验

`src/experiments/` 保留全部研究分支。文件名基本对应方向：`hnsw_block_graph*` 是 block graph/portal，`aggregated_mac_hnsw` 是 MAC，`sq4_progressive_adjacency` 是 primary/residual adjacency，`verification_block_screen` 是 verification-aware block screen，`residual_*`/`pq_*` 是早期 residual/PQ 树。相应说明在 `docs/notes/`，原始结果在 `results/archive/`。

