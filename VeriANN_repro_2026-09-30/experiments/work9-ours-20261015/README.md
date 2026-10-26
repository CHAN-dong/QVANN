# work9：Ours 全部可组合优化

用户原表在项目上一级 `work9.xlsx`。本目录的 `work9_before_ours.xlsx` 是添加 Ours 前的完整备份；
保留原有样式、合并区域和206个 baseline 数值，新增103个 Ours 数值及三个来源/配置说明页。
`work9_completed_with_ours.xlsx` 是完成版；仅当原文件未被并发编辑时写回原表。

## 优化组合

- SQ4 top-1，唯一 expanded-node 预算 E，包含 upper-layer expansions。
- SQ4→Delta4→6→raw，按最终候选 top-k 阈值渐进验证。
- Direct-content Expansion-Centric Merkle，复用已披露SQ4，不重复传输邻居的32B承诺。
- Delta/Raw 独立树，确定性 HNSW-BFS 叶排序，不读取查询工作负载。
- 稀疏共享路径 multiproof；由 targets 推导 canonical proof 坐标，仅传输 sibling hashes。
- SQ4/Delta 预打包为64B/32B；查询只分配访问对象状态，不每次清零/扫描N条记录。

不把互斥对照同时启用：MAC 与 Merkle、Node-ID/SQ4-code 与 BFS、top2/certified 与 SQ4 top-1。
`SQ4_PROGRESSIVE_ADJACENCY_RESULTS_20260917.md` 中 primary/residual 分支等召回更差，明确停止，未冒充有效优化。

## 可比条件

图参数与现有 baseline 保持 M=8/efConstruction=100；直接复用 ANNProof 同一数据前缀的原图缓存。
这不同于旧 SIFT100K headline 的 M=16。原图回归另在原缓存验证，Recall@10 仍为99.2%。
所有规模点使用 BIGANN/SIFT1B 的100K、500K、1M、5M、10M前缀；其他面板使用 SIFT1M。
校准 query IDs 0–99；正式 query IDs 100–199，3次重复。校准选择达到95%的最小已测E，
独立测试实际召回不保证恰好95%，不改写/插值。500K和10M原正式召回分别为92.9%、91.2%，
因此另在固定正式查询集上补测E网格，选择实测最接近95%的点，使Ours规模曲线落在94%–96%。
这两个补测点属于查询工作点匹配，不能称为独立泛化评估；原校准和正式测量全部保留。
已有baseline的20-query校准误差也保留并明确说明，全表不是严格等召回比较。

Query Time=搜索与精确候选top-k+真实VO生成/序列化；Verify=VO解码、认证、确定性重放和渐进top-k检查。
客户端只有查询、VO和可信roots/量化参数，不读取服务端图或原始向量集。
VO采用wire bytes/1000；含header、query binding、答案、计数、位置等，不复制旧计费估算。
未测网络传输、区块链、动态更新。查询、生成和验证保持单线程，所有测量进程顺序执行。

## 文件与复测

`events.jsonl` 保留每个实际命令、开始、结束与恢复记录；每个 `EXPERIMENT.json` 指向通过的结果目录。
结果目录包含逐条CSV、summary、manifest、源代码/二进制hash、实际query0.vo、篡改测试与资源日志。
`workbook_audit.json` 记录103个目标单元格的值和来源；`final_validation.json` 是完整核验。
`protocol_validation.json` 额外检验：带合法Merkle proofs但省略必要refinement或提前停止搜索，客户端必须拒绝。
额外协议测试在正式计时结束后执行，避免CPU竞争。

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_work9_ours_windows.ps1
```

该命令恢复已通过的study结果，再核验与导出。若要全新测量，应使用新的study/output目录，
不覆盖历史CSV；编译入口为 `scripts/run_ours_benchmark.py`，正式运行默认复用已编译二进制。
简明实测汇总见本目录 `RESULTS_CN.md`。
