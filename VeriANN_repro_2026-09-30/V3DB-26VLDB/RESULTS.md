# 本机运行结果

日期：2026-10-16（Asia/Shanghai）。作者 commit：`a82ac732828591bd49b83d8f31c56fa4bd1cd9b2`。原版源码无修改。

以下为一次运行的实测，不是论文的五次重复统计；4 个 Rayon 线程，OpenMP/OpenBLAS 为 1 线程。电路 build 时间不包含在 prove 列中。

| 测试 | build 秒 | prove 秒 | verify 毫秒 | 压缩证明字节 | prove 采样峰值 RSS GiB | build 前门数 |
|---|---:|---:|---:|---:|---:|---:|
| smoke multiset | 0.0332 | 0.0613 | 2.586 | 95,579 | 0.053 | 429 |
| smoke circuit-only | 0.0560 | 0.1016 | 3.030 | 102,979 | 0.070 | 843 |
| 论文 basic multiset | 1.9426 | 2.6880 | 5.088 | 146,046 | 1.076 | 21,198 |
| 论文 basic circuit-only | 8.2849 | 10.9162 | 4.598 | 159,098 | 4.136 | 101,016 |
| SIFT4096 首条 query multiset | 1.1045 | 1.2787 | 3.955 | 134,790 | 0.656 | 8,181 |

所有正向测试均由作者 Rust 函数实际调用 `data.prove()`、压缩证明并调用 `data.verify()`，未使用耗时估算或假证明。basic 两种实现的门数与论文 Table 8 完全一致，本机这一次 prove 加速约 4.06 倍。

## 真实 SIFT 子集

- 读取现有 `dataset/sift/sift_base.fvecs` 前 4,096 条与 `sift_query.fvecs` 前 10 条，维度 128。
- 参数：32 个列表，探测 4 个列表，M=8，K=16，top-k=10，训练 10 次迭代，随机种子 0。
- 作者容量约束重平衡到每簇 128 条，迁移 717 条；实际 padding 容量 128。
- 10 条查询的集合 Recall@10 平均 0.31，在该 4,096 条子集上重新计算 exact ground truth。此参数仅用于跑通完整链路，不代表论文全量精度。
- 第一条查询实际生成并内部验证带 Merkle 证明；Python 的 proof/fast 返回 top-k 相同。其余九条只测检索。

## 负向测试

修改已打开列表的一条 PQ code，保留原来的列表承诺根。作者证明路径返回 witness 冲突的 `RuntimeError`，负向测试通过。原始拒绝原因见 `results/tamper-multiset.json`。这检查生成阶段的承诺一致性，尚未测试独立客户端对传输证明的解码和拒绝。

## 适用范围

`results/suite.json` 中 6 项均为退出码 0；`results/validation.json` 校验 basic 门数、负向拒绝、作者仓库无受控源码改动，以及输入数据哈希。

作者默认 Plonky2 配置 `zero_knowledge=false`，以及 multiset 挑战约束的源码观察，详见 [README](README.md#测量边界与源码观察)。本次完成作者默认实现的功能与成本复现，没有声称完成隐私或恶意 prover 安全证明。

未执行论文 large、全量三个数据集、五次重复置信区间或全部参数扫描。
