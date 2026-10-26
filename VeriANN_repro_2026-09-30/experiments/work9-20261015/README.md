# work9.xlsx 实测填表

模板：项目上一级 `work9.xlsx`。原表快照备份在本目录 `work9_original.xlsx`。
输出：`work9_completed.xlsx`；各单元格数值、来源及待测列表在 `workbook_audit.json`。
最终完成后更新原文件。过程中没有通过完整验证的数据标为“待测”，不填估算或插值。

## 口径

- Query Time = 搜索 + VO 生成；客户端验证单独计时。
- VO 单位采用十进制 KB，即 wire bytes / 1000，区别于原始 CSV 的 KiB。
- Recall 写为百分数。
- 正式测试：100条查询，IDs 100–199，完整查询集重复3次（每配置300样本）。
- 规模校准：不同的 query IDs 0–19，只用来挑选测试预算。
- 基线为两个目录内已披露假设的链下算法重实现；没有声称原论文全部系统/数值复现。

## 实验块

1. SIFT1M 总体：ANNProof ef=100；VLAH ef=50, probes=41；均 M=8, efc=100。
2. Recall@10 曲线：每方法11种实测预算，三张曲线使用同一组记录。
3. K 曲线：1/5/10/20/50/100，ef=100，VLAH probes=41。
4. 规模曲线：100K/500K/1M/5M/10M，统一 BIGANN/SIFT1B 前缀来源。
   原模板标“SIFT1M”却包含5M/10M，已对三块规模实验明确改标来源。

规模预算取在独立校准集上达到至少95% recall的最小已测试 ef/probes；
这不是保证所有正式测试点的 recall 精确相同。实际值在“实验配置与实测召回”
及单元格注释中完整列出，解读曲线时需要考虑此差异。

大规模首次建图可用8线程；每次建图后恢复1线程，查询和验证保持单线程。
所有测量进程顺序运行。索引缓存按输入内容、成员和图参数区分，避免反复建图。
多线程构建可能导致新建图拓扑变化，复测已有快照使用缓存。

## 来源

- SIFT1M：`dataset/sift1m/provenance.json`，ANN-Benchmarks HDF5 镜像转换。
- BIGANN：`dataset/bigann10m/provenance.json`，官方原始1B文件前10M字节范围。
  Range 响应和各块同一源 ETag 均核对；转换为 float32 fvecs，独立重算所选规模 GT。
  只下载前10M，未下载整个1B。未将重复1M向量当作5M/10M数据。
- 来源 URL：`https://dl.fbaipublicfiles.com/billion-scale-ann-benchmarks/bigann/base.1B.u8bin`。
- `events.jsonl`：实验开始/结束/失败/恢复时间和实际执行命令。
- `METHOD-EXPERIMENT.json`：对应通过结果目录及汇总；文件内命令、数据/源代码哈希留档。
- 失败日志不参与填表（曾因新增 CLI 参数对应旧二进制失败，重新编译重测后才填入）。

## 重现

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_work9_windows.ps1
```

该命令恢复本 study 已通过的工作并继续剩余工作，不会将恢复当作重新测试。
如需全新测量，复制/修改脚本中的 STUDY ID，保留索引缓存并使用新的空结果目录。
参数矩阵入口：`scripts/run_work9_experiments.py`；Excel 导出入口：`scripts/fill_work9.py`。
仅在原文件没有被并发修改时使用 `--update-original`，否则完成版副本仍然保存。

Workbook 增加“实验说明”“实验配置与实测召回”“单元格来源”，保留主表布局与合并单元格。
K 实验两处误写为 Dataset size 的标题已更正。导出后重新读取，检查全部206个目标单元格
与原始CSV计算值一致，并核对实际VO文件字节数及所有诚实/篡改检查的通过状态。

另增加“资源与尾延迟”页：Query P50/P95 从逐条 search+VO generation 总时间计算，
采用经验 nearest-rank 分位数；包括 Verify P95、峰值 RSS、索引阶段（构建或加载）、
ADS 构建和精确 GT 耗时。缓存加载时间不代表首次建图时间。

已观察到 BIGANN 1M 的 ANNProof 校准召回为95.5%，正式测试为87.6%。
20条校准查询不能保证测试集达到95%，因此规模曲线不是严格等召回性能比较。
完整保留实际召回，不更改、筛掉或插值这类结果。

简明实测报告由 `python3 scripts/summarize_work9.py` 写入本目录 `RESULTS.md`。
