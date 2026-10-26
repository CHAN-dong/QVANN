# 两篇论文复现：本机结果与完成范围

日期：2026-10-14。已阅读两份提供的 PDF，完成两个目录中的链下算法重实现、
Windows/WSL 入口和测量框架，并实际跑通 SIFT100K、SIFT1M。
**不是原论文全部系统/图表的严格数值复现。** 未部署区块链，未运行论文全部对照
算法和全部数据集。VLAH 有多项论文未给出的执行细节，详见其 README。

## 主要结果：SIFT1M

本机 i9-12900，Ubuntu 26.04.1 / WSL2，GCC 15.2.0，单线程。
SIFT1M 128D，前 100 条查询，k=10，M=8，efConstruction=100。
以下默认设置分别独立、顺序运行 20 次 query set，共 2000 次测量。
这两行的召回不同，不适合直接当作同精度下的算法优劣比较。

| 设置 | Recall@10 | 查询均值 ms | VO 生成均值 ms | 验证均值 ms | VO 均值 KiB |
|---|---:|---:|---:|---:|---:|
| ANNProof，ef=50，shard=10000 | 0.896 | 0.404111 | 1.16978 | 7.96340 | 617.281 |
| VLAH，ef=50，bucket=20000，probes=3，w=256 | 0.373 | 0.659854 | 2.16586 | 7.70271 | 1130.47 |

原始记录：

- ANNProof：`ANNProof-24FGCS/results/20261014-233332/`。
- VLAH：`VLAH-25TrustCom/results/20261014-233541/`。
- 逐条 CSV、P50/P95、独立认证/重放耗时、真实 query0.vo、root registry、
  8 / 11 类篡改测试、进程资源日志均保留。
- 两组 2000 次正常验证全部通过；所有篡改测试被拒绝；文件大小与记录 VO 字节数相符。

## 首次构建

| 方法 | index setup | ADS build | 整个进程墙钟时间 | 峰值 RSS |
|---|---:|---:|---:|---:|
| ANNProof 1M | 240.890 s | 7.860 s | 263.63 s | 2293.44 MiB |
| VLAH 1M | 67.387 s | 1.800 s | 88.00 s | 1370.58 MiB |

首次构建目录分别是 ANNProof `20261014-232555`、VLAH `20261014-233107`。
index setup 包括图记录转换；VLAH 还含 LSH、排序和分桶。
后续缓存加载的 setup 时间不能作为首次建图时间。
这些首次运行的精确墙钟时间有少量后台检查/编译测试干扰，作为本机记录而非
严格隔离的跨算法构建性能结论。后面的 20 次重复查询测量是顺序运行。

## 参数扫描与准确度问题

SIFT1M 的 ef/probes 扫描（100 条查询，单次 set）：

| 方法/设置 | Recall@10 | VO 均值 KiB |
|---|---:|---:|
| ANNProof ef=50 | 0.896 | 617.281 |
| ANNProof ef=100 | 0.951 | 1014.18 |
| VLAH probes=3 | 0.373 | 1130.47 |
| VLAH probes=5 | 0.437 | 1860.88 |
| VLAH probes=9 | 0.544 | 3297.03 |
| VLAH probes=29 | 0.852 | 10423.7 |
| VLAH probes=41 | 0.954 | 14155.0 |
| VLAH probes=50（访问全部桶） | 0.993 | 16747.6 |

probes=29/41/50 在 `VLAH-25TrustCom/results/20261014-233755/`。
高 probes 扫描是诊断/功能实验，其间有约两秒的另一个小型回归进程，
所以不把该扫描的精确耗时当作正式独立性能测量；VO 和 recall 不受此影响。

可见，在本次明确披露的 VLAH 实现选择下，有限桶路由明显影响准确度。
提高访问桶数能恢复召回，但增加 VO；还没有复现论文报告的 VO 优势。
这不能直接推导为原论文实现必然更差，因为投影/编码/桶键/Mt/Mj 等缺少确切定义，
且本版本发送完整搜索子图以便客户端重放，没有采用不完整说明的 digest 简化。
**验证通过只保证承诺索引上的算法一致执行，不证明全库 epsilon 完备性。**

## 完整性和边界验证

- 120 个稀疏 Merkle 测试：SHA-256 / Keccak-256、multiproof / 独立路径、
  n=1、奇数、2 的幂及相邻大小，正常验证与篡改/截断拒绝。
- ANNProof：默认分片、无分片 ANNProof-U、独立路径、k=1/10/50、ef 列表。
- VLAH：多桶、尾桶不足容量、独立路径、k=1/10、不同 probes。
- 正常 VO 真实解码、叶哈希重算、验证根、重放；完整图未传入客户端验证函数。
- 所有成功目录均有 PASS.txt；失败/未完成目录不能当作有效结果。

100K 的功能扫描及重复测量保留在两个目录的 results 下。
全部通过配置的索引表：`docs/paper_repro_all_runs.csv`，包含数据哈希和参数，
需要按 base、queries、k、ef、proof、repeats 等字段过滤，不能把不同规模混合。

## 使用入口

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ANNProof-24FGCS/run.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File VLAH-25TrustCom/run.ps1
```

百万数据已准备在 `dataset/sift1m/`，增加
`-Dataset dataset/sift1m -Base 1000000 -Queries 100 -Repeats 20` 即可。
修改源码后不要使用 -SkipBuild。每次创建独立结果目录。
