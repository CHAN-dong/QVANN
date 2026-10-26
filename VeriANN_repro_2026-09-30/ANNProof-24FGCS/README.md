# ANNProof (FGCS 2024) 算法复现

本目录实现论文 §§4.1–4.4 的链下查询/认证协议，可以实测查询时间、VO 生成时间、
客户端验证时间、实际 VO 字节数和召回率。它是 **按论文重实现的算法复现**，
不是作者原程序的原样运行，也不是论文图表数值复现。

## 论文与作者源码

- 论文：`ANNProof: Building a verifiable and efficient outsourced approximate nearest neighbor search system on blockchain`，FGCS 156 (2024), 206–220，DOI `10.1016/j.future.2024.03.002`。
- 输入 PDF：项目上一级 `papers/ANNProof-24FGCS.pdf`；提取文本在 `paper/paper.txt`。
- 作者代码：https://github.com/lulinglingcufe/vnmslib ，§参考文献 [18]。
- 已下载并选择性保留 `upstream/` 的作者源码。快照 commit
  `189067d8a708c4236f22ee8a791167599830c6ab`；API 元数据在 `paper/upstream_commit.json`。
- 原归档 `upstream.zip` SHA256：`171ed67b577bb33407989528b6f07c5608eb2523c9288b39f3dc2e8a5a1b1877`。
- 使用作者附带的 Keccak-256 实现，正常运行时检查已知空串哈希。

## 实现对应关系及范围

| 论文 | 本实现 |
|---|---|
| Algorithm 1，HNSW index | 项目内修改版 Faiss 建图；M/efConstruction 可配置 |
| Eq. (3) 节点摘要 | Keccak-256(H(id) ∥ H(vector) ∥ H(all-layer adjacency)) |
| Eq. (4) 父摘要 | Keccak-256(left ∥ right) |
| Eq. (5) 分片 | 连续 node ID 分片，默认 shard_size=10000 |
| Algorithm 2 | 标准 HNSW 搜索、记录所需子图、生成真实 Merkle 证明 |
| Algorithm 3 | 只从收到的序列化 VO 重建子图、认证并重放同一搜索，比较 top-k 和 trace |
| Eq. (7) identifier tree | 单独认证返回对象的 ID/vector 映射 |
| ANNProof-U | shard_size 设置为整个数据集大小，无分片对照 |

**具体差异：**作者用 NMSLIB，本实现用 Faiss 建图和显式确定性 HNSW 重放，
因此图拓扑及召回可能不同。采用长度明确的小端二进制编码，不使用作者程序中
不安全的 sprintf/二进制 C 字符串处理。所有触及节点发送完整各层 adjacency，
在用户独立重放时拒绝缺失节点。原始查询为外部提供的向量，本实验未实现按账户 ID
取得 query vector 的 API；identifier tree 验证返回对象映射。

默认用共享路径 multiproof，也可使用独立路径作为对照。Merkle prove/verify
采用稀疏路径算法，不扫描整棵树。真实收到的二进制协议大小会高于仅计 32B
hash 的理论证明大小，两个口径在 per_query.csv 中分开。

**区块链部分：**采用本地可信、不可由 SP 修改的 root registry 模拟外部承诺；
未部署 Fabric，未测共识、交易、链上访问、更新或网络延迟。验证计时不包含这些成本。
认证保证的是已承诺索引上指定 ANN 算法的诚实执行，不能把通过认证解读为全库精确
top-k 或全局 ε-近似保证。

## Windows 运行

在项目根目录：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ANNProof-24FGCS/run.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ANNProof-24FGCS/run.ps1 -K 1,10,50 -EfSearch 20,50,100 -Repeats 20
```

默认 SIFT100K、100 queries、k=10、M=8、efConstruction=100、efSearch=50。
百万规模：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ANNProof-24FGCS/run.ps1 -Dataset dataset/sift1m -Base 1000000 -Queries 100
```

Linux/WSL 可直接 `python3 ANNProof-24FGCS/run.py --k 1,10,50 --ef-search 20,50,100`。
`--shard-size 100000` 对 100K 禁用分片；`--proof paths` 测独立认证路径。
`--skip-build` / PowerShell `-SkipBuild` 仅用于已编译且未修改代码时。
`--metric cosine` 先归一化，再使用等价的 normalized L2 排序。

输入目录内需要 `sift_base.fvecs`、`sift_query.fvecs`（名称固定，维度可变）。
大于现有文件长度时明确失败；不会静默将 100K 当作 1M。
ground truth 在所选 base 子集上重新精确计算，不误用其他规模的 GT。

## 输出与验证

每次输出到新的 `results/时间戳/`。详见项目 `docs/PAPER_REPRO_METRICS_CN.md`：
逐查询 CSV、均值/P50/P95 汇总、可信根、真实 query0.vo、资源日志、参数和环境清单。
验证解码、哈希认证、子图重放均真实执行；不会用服务器算好的布尔值冒充验证。
默认 8 类篡改测试必须全部拒绝；正常查询必须全部通过；query0.vo 文件长度
必须与记录的 vo_bytes 完全一致，运行器才写 PASS.txt。

首次创建的新 M=8 索引与旧 VeriANN M=16 索引分开，缓存按数据内容及参数命名。
`index_ms` 包含加载/建图及构造可认证的图记录，缓存运行不是新建索引时间。
首次建图记录与重复实验记录应分别报告。
