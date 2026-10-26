# ANNProof / VLAH 实验口径

两个目录分别提供独立算法入口 `annproof.cpp` 和 `vlah.cpp`、Windows `run.ps1`
及 WSL `run.py`。共同的二进制传输、Merkle、HNSW trace 与测量基础设施在
`src/paper_repro_common.hpp`，不是调用原 VeriANN SQ4 实验换一个名称。
ANNProof 作者源码留在 `ANNProof-24FGCS/upstream/`；VLAH 用论文算法重实现。
所有重实现差异必须和两个 README 一起阅读。

## 数据和参数

- 默认 SIFT100K，128D，100 queries，k=10，M=8，efConstruction=100，efSearch=50。
- ANNProof 分片 10000；VLAH 桶容量 20000、默认探测 3 桶、w=256、LSH seed=42。
- `--k`、`--ef-search`、`--probes` 支持逗号分隔列表；repeats 是对整个 query set
  的重复次数。`queries=100,repeats=20` 表示 2000 个样本，不是 2000 条不同查询。
- efSearch 小于 k 时使用 max(efSearch,k)，实际值记录到 CSV。
- Faiss 插入图使用 vendored 默认随机种子；CLI seed 仅用于 LSH 投影。
- 固定单线程，索引缓存按有效数据内容、桶成员、M 和 efConstruction 区分。
- 精确 ground truth 对所选 base/query 子集重新计算，GT 计算不进入查询计时。

SIFT1M 来自 ANN-Benchmarks 提供的 `sift-128-euclidean.hdf5` 镜像，
`scripts/prepare_sift1m.py` 分块转换为 fvecs/ivecs，完整哈希与形状在
`dataset/sift1m/provenance.json`；已确认其前 100K base 及前 100 queries
与本项目原数据完全一致。原有 `dataset/sift/` 没有修改。

## 计时

| 字段 | 包含什么 |
|---|---|
| search_ms | 服务端实际搜索；VLAH 还包括 query LSH/Morton、选桶和合并候选 |
| vo_generation_ms | 真实 Merkle/Trie 证明生成、所有所需记录/trace/结果的二进制序列化 |
| authentication_ms | 客户端解码 VO、检查查询参数和路由、重算叶子和认证根 |
| replay_ms | 从解码的部分索引重放搜索、检查 trace 和最终 top-k |
| verification_ms | 整个客户端验证调用，包括解析、认证、重放及局部对象析构 |
| server_total_ms | search_ms + vo_generation_ms |
| end_to_end_compute_ms | server_total_ms + verification_ms，无网络、区块链获取或传输 |
| index_ms | 新建或加载索引并转换为认证图记录；VLAH 包含编码、排序、分桶 |
| ads_ms | 节点摘要、Merkle 树和全局 Trie 构建 |
| ground_truth_ms | 独立精确 ground truth 计算，非查询阶段 |

`verification_ms` 通常稍大于 authentication_ms + replay_ms，因为包括栈展开/
容器析构。`steady_clock` 实测，不用推算替代。索引缓存后的 index_ms 不是首次
建图时间；ANNProof 另有 `index_state.txt`，首次跑与缓存跑应分别报告。
当前没有显式预热阶段，第一条测量包含其冷缓存影响。重复测试报告均值/P50/P95。

## VO 大小

- `vo_bytes = 实际收到的二进制 buffer 长度`；`vo_kib = vo_bytes / 1024`。
- `query0.vo` 保存首个完整 VO，运行器核对文件长度与第一行 vo_bytes 一致。
- `record_bytes` 包含 local position、global ID、维数、float32 vector、各层 adjacency
  的长度/ID，以及 VLAH 的边距离。
- `local_proof_bytes` 包含真实局部证明及 position/level 等编码元数据。
- `route_proof_bytes` 是 VLAH 全局压缩 Trie 子树证明的实际编码字节。
- `vo_bytes` 还含 query hash、k/ef、结果 IDs、trace、分组/计数等协议头开销；
  所以不能只把三个组件相加后认为等于总 VO，也不能只报告 sibling_hashes*32。
- 默认 multiproof 去重；`--proof paths` 发送独立局部路径，包括重复部分，
  所有冗余哈希也必须与已认证节点一致。
- 返回结果与 proof 在同一个 wire buffer 中计算一次，无额外重复计费。
- root registry 与 LSH 公共参数属于预置客户端状态，不按每条 query VO 收费；
  链上获取这些承诺的时间和字节未测量。

## 准确度

`recall_at_k` 为返回结果与真实 top-k 的交集大小除以 k。
`recall_at_1` 是第一条返回结果恰好为真实第一近邻的比例。
`kth_distance_ratio` 为返回第 k 近邻与真实第 k 近邻的 Euclidean 距离比。
cosine 模式先归一化再按 L2 排序，所以该比值是 normalized L2 比值，
不是 cosine distance 比值。重复样本的准确度不会当作不同查询的准确度。
认证成功不代表 recall=1，也不证明所有未访问区域没有更近的结果。

## 文件及完整性测试

- `per_query.csv`：每次查询的全部上述字段，另含 visited/expanded/buckets。
- `summary.csv`：每组 k/ef/probes 的均值、P50/P95、召回率、验证通过率、构建和内存。
- `manifest.json` / `environment.json`：数据内容哈希、参数、编译器、平台和完整执行命令。
- `resources.txt`：`/usr/bin/time -v` 的全过程墙钟时间和峰值 RSS 等。
- `peak_rss_mib`：进程最大驻留内存，含 build/load/ADS/GT/query。
- `ads_bytes`：Merkle 树有效 hash 数组的字节数；不含容器 capacity、对象、Trie
  heap 和索引，不能当作整个进程 RSS。
- `trusted_roots.txt`、VLAH `lsh_parameters.txt`：预置的外部承诺和公共投影参数。
- `build.log` / `run.log`：原始编译及实验日志。
- `tamper_tests.csv`：向量、邻接、结果、证明、遗漏记录、错误根、错误 query 绑定；
  VLAH 额外检查路由和 trace 修改。
- `PASS.txt`：仅在所有 honest queries、全部篡改测试和 wire 大小核对通过后生成。

原始结果写入不同目录；非空输出目录会被拒绝。查询验证器不访问原完整图/
数据集/服务器 Merkle 对象，只用收到的 bytes、query 和可信公开锚点。
120 个独立稀疏 Merkle 属性测试在 `scripts/test_paper_primitives.cpp`，覆盖两个
hash、两个 proof 模式、奇数和 2 的幂大小、篡改及截断编码。

## 如何公平比较

比较两个方法时先扫描 efSearch 与 probes，使 recall 接近，再比较 VO、查询与
验证成本；默认 ef=50 并不让两种方法有相同 recall。不同 hash、发送内容和协议头
的差异要公开。参数扫描过程中若并行运行两个进程，时间会受到 CPU 竞争影响；
正式计时应顺序运行，每组至少多次重复。现有目录中同时保留快速功能扫描与顺序
重复测量，结果不应混用。

本工程完成的是链下算法与测量工具复现。没有把 Hyperledger Fabric/其他链的
系统性能、论文所有对照算法、全部数据集或原论文数字宣称为已复现。
