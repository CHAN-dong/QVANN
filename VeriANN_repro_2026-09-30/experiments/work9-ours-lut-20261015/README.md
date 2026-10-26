# work9.xlsx：查表版本完整计时更新

用户所指工作簿在项目上一级；目录中现有文件名为 `work9.xlsx`。
`work9_before_lut.xlsx` 是本轮更新前的完整备份，含原有baseline和Ours结果。
`work9_completed_lut.xlsx` 是新完成版，全部测量通过后写回原文件。

## 更新内容

Ours总体、11点Recall曲线、5点BIGANN规模曲线和6点K曲线全部用同一已验证的
VeriANN-LUT二进制重新测量，共23个正式配置、6900条逐查询记录。
刷新103个Ours单元格及其来源，其中查询/验证时间46格；其余57格与旧版一致。
保留原206个baseline数值以及工作簿原有合并布局。

每个配置固定原来的数据、缓存图、M=8、efConstruction=100、E、k、query IDs100–199、3次重复。
500K和10M沿用原工作点匹配选择的E=112、512，保留该选择并非独立泛化评估的说明。
不重新校准，不通过改变召回率来改变计时，也不按单点加速百分比缩放其他格子的旧值。

Query Time为search+真实VO生成/序列化；客户端验证含解码、哈希认证、搜索重放和渐进top-k检查。
服务器与客户端各自构建查询查表，准备成本均计入相应时间。单位ms，VO使用十进制KB。
没有包含网络或链上操作，所有查询及验证保持单线程，测量进程顺序运行。

## 核验

`frozen_workpoints.json`记录每个原工作点及来源；`events.jsonl`记录实际执行命令和完成状态。
每个结果目录中的`scalar_equivalence.json`逐条核对旧版和新版所有非计时CSV字段，
包括质量、访问/展开数量、VO字节数和各类proof字节数；三个可信根也必须一致。
每次运行独立检查诚实VO、13类篡改、实际样本VO大小。源代码和二进制hash均归档。
额外协议测试沿用同一二进制指纹已通过的查表版检查结果，保存在本目录protocol_validation.json。

原表备份、各格子的值与来源、最终核验分别在`work9_before_lut.xlsx`、`workbook_audit.json`、
`final_validation.json`。配置、来源和统计口径同时写入工作簿的三个Ours附加页。
实际新旧时间对比见`RESULTS_CN.md`。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_work9_lut_windows.ps1
```

该命令恢复本轮已通过的测量，核验并导出；全新测量需使用新的study/output目录。
依赖已验证的`build/veriann_lut_benchmark`及对应源文件；查表变体的生成与初始对照见
`scripts/prepare_ours_lut.py`和`experiments/ours-lut-20261015`。
