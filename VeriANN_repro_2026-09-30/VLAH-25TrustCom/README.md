# VLAH (TrustCom 2025) 算法复现

本目录实现 LSH/Morton 分桶、压缩前缀 Trie、桶内 HNSW、两层 Merkle 认证、
客户端搜索重放，并记录查询/证明/验证时间、真实 VO 大小及召回。
**这是披露实现选择的算法复现，不是作者源码或论文数值的精确复现。**

论文：`VLAH: A Lightweight and Verifiable Framework for Approximate Nearest Neighbor Search in High-Dimensional Space`，DOI `10.1109/Trustcom66490.2025.00187`。
输入 PDF 位于项目上一级 `papers/VLAH-25TrustCom.pdf`，提取文本在 `paper/`。
在提供的论文中没有找到 VLAH 官方代码链接；实现以 §§IV–V、Algorithms 1–3 为依据。

## 实现及未明确细节

| 论文模块 | 本实现 |
|---|---|
| Eq. (1) Euclidean LSH | 8 个固定种子的 Gaussian p-stable 投影，floor((a.x+b)/w) |
| bitstring/Morton 编码 | 每个投影编码为加 128 偏置的 8 bit（超出范围截断），按 bit interleave 成 64 bit |
| 固定容量分桶 | 按 Morton code、ID 排序，默认每桶 20000 个向量 |
| 压缩 prefix trie | 以桶首完整 Morton code + 32 bit bucket ID 构造二进制 Patricia trie |
| 邻桶 | Morton 排名中主桶左右交替，默认共探测 3 桶 |
| bucket graph | 每桶独立 HNSW，默认 M=8、efc=100、efs=50 |
| leaf hash | SHA256(global ID ∥ SHA256(vector) ∥ SHA256(adjacency)) |
| adjacency | 各层完整邻接表，含 local node ID 和节点间 Euclidean 距离 δ |
| trie hash | SHA256(length-delimited prefix ∥ child hashes)，leaf 绑定完整桶元数据与桶根 |
| 两层认证 | 最小覆盖 Trie 子树证明 + 每桶稀疏 Merkle multiproof |
| trace 验证 | 发送完整所需子图，客户端认证后重放；未启用可选 TraceDigest 简化 |

论文未给出具体投影数量/分布参数、量化位串方式、w、Morton 交织规则；
示例使用“前 20 bits”，实验段落又使用“后 20 bits”作为桶键，同时要求排序后按
20000–50000 固定容量分桶。单独的 20-bit 桶键无法一般地满足唯一键、固定容量
和完整排序区间路由三个条件。因此本实现采用完整 Morton key + bucket ID
消除冲突，并披露这项差异。**这会影响召回率和 VO，不能用于断言论文报告的
30–50% 改善已经复现。** CLI 的 `--lsh-width`、`--seed`、`--bucket-size` 和
`--probes` 可用来做敏感性实验；不应只选有利的参数。

论文 Algorithm 2 的 access ≪ Mj、Mt、层循环方向没有给出完整可执行定义。
本实现补充为标准 HNSW 的 top-down greedy + level-0 efSearch 搜索；
这是明确的执行策略选择，不能等同于拿到作者原实现。

为了认证路由，Trie leaf 额外绑定 bucket ID、count、entry、maxlevel、Morton
区间和桶根。主桶及其 successor 的证明验证 predecessor 区间，邻桶按照已约定
政策验证，避免 SP 任意选择不同的桶。所有触及节点而非仅 top-k 节点都必须带
完整内容及证明，否则无法独立重放；这通常增加 VO。

当前保证是承诺索引上的路由、节点完整性及指定搜索算法的一致执行。
**不会把通过认证当作全局精确 top-k 或 (1+ε) 完备性证明。** 有限邻桶和 HNSW
本身不能无条件推出该保证。召回通过独立精确 GT 计算，可能低于 ANNProof。

区块链外部根采用离线可信 registry。未部署链、未测网络/交易/共识，也未实现
动态更新、Naive 基线或论文全部数据集。原论文的可选 digest 模式没有充分说明
如何独立绑定每次 query trace，当前使用明确的完整重放模式。

## 运行

在项目根目录的 PowerShell：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File VLAH-25TrustCom/run.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File VLAH-25TrustCom/run.ps1 -Probes 1,3,5 -K 1,10,50 -Repeats 20
powershell.exe -NoProfile -ExecutionPolicy Bypass -File VLAH-25TrustCom/run.ps1 -Dataset dataset/sift1m -Base 1000000 -Queries 100
```

WSL/Linux：`python3 VLAH-25TrustCom/run.py --probes 1,3,5 --bucket-size 20000 --lsh-width 256`。
支持同 ANNProof 的 k/ef/M/efc/metric/repeats 参数。`--proof paths` 是独立局部
认证路径对照，全局 Trie 仍合并路径。cosine 输入需选适合归一化尺度的 width
（例如探索 0.05–0.5）；默认 256 仅是当前原始 SIFT 尺度的实现选择。

输入、CSV 和计时口径见 `docs/PAPER_REPRO_METRICS_CN.md`。
每次自动创建独立结果目录，保存真实序列化 VO、原始查询 CSV、参数、LSH 投影、
承诺、资源日志和篡改测试。默认 11 类篡改全部必须拒绝；不通过就非零退出。
缓存按数据内容、桶成员和 HNSW 参数隔离，避免复用错误规模或错误分桶的索引。
