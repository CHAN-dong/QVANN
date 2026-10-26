# NYTimes-256 替换实验

用户明确选择 NYTimes-256，替换主评测中的 GIST1M。全部方法和原表中的受控消融均重测；不预设新数据上 Ours 的表现。

- 来源：https://ann-benchmarks.com/nytimes-256-angular.hdf5
- 基准定义：https://github.com/erikbern/ann-benchmarks
- 源基库 290,000 条，256 维，查询库 10,000 条。零范数审计发现基库239条、查询9条零向量。统一剔除基库239条后实际289,761条，保存原ID映射；查询保留原始行号，所用IDs100–119均非零，9条零查询不参与评测。单位归一化后的 float32 平方 L2 表示 angular 排序。
- 查询 IDs 100–119，每条真实重复 3 次，查询/VO/验证单线程。构图可使用 16 线程，与原新增数据协议相同。
- ANNProof/VLAH/Ours 主表：统一 M=16、efConstruction=200；同工作负载选参数；ANNProof 保留完整优化版本。
- V3DB 保留作者 IVF-PQ 实现，允许按原来同样的 PQ 网格调参；单线程证明；50 GiB 进程地址空间限制。目标不可达/资源不足明确标注。
- 保留原规模列：100K 适用；500K、1M、5M、10M 超出容量，标“不适用”。总体、召回曲线、k 曲线及受控消融使用完整 290K。
- 每个 N 重新计算精确 ground truth，不能沿用 GIST 或其他基库规模的真值。

`work9_before_nytimes.xlsx` 是替换前完整工作簿，`gist_archive_manifest.json` 索引保留的 GIST 原始数据。归档保留在原路径；主表及辅助数据中旧 GIST 测量从活跃评测移出。不得把这些实验描述为事先选定数据集或删除不利结果后得出的无偏结论。

入口：`scripts/work9_nytimes.py`。`prepare` 下载校验并转换；`fill` 从真实完成记录更新；`run` 顺序运行主比较、受控消融、V3DB，并恢复其他数据集尚未完成的 V3DB 实验。

旧 V3DB 调度器已停止以免覆盖新版工作簿；旧WSL会话随后结束，DEEP当前未完成样本不计入，36条已落盘样本保留，可断点续跑。新计时不与它并发。

进度和原始输出：`runner-active.log`、`events.jsonl`、`evaluation-*.json`、`ablation-evaluation-*.json`、`results/work9-nytimes-20261017/`。`runner.log` 保留最初发现零向量时中止的预处理记录，不是当前运行日志。`待测` 不代表测量值，只有完整 60 次且通过实际 VO 核验的工作点才填数值。

工作簿副本：`work9_nytimes.xlsx`；完整生成文件也保存在 `../work9-v3db-20261016/work9_with_v3db.xlsx`。原 `work9.xlsx` 被 Excel 占用时不强行覆盖，副本仍更新。核验入口：`scripts/verify_work9_nytimes.py`；其报告明确区分已完成、待测及资源/召回限制。
