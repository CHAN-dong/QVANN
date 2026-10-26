# V3DB 单次实测（2026-10-20 用户最新口径）

只改变 V3DB：每个配置固定 query ID 100、repeat 0，只生成/验证一次真实证明。相同配置已有该样本时直接复用，不挑选最快值，不再测20×3。查询/验证耗时和VO均为单次值，不声称平均值或稳定统计量。

Recall@k 单独取既有同配置20条查询的搜索校准，明确标注不是20次证明；中间探测预算无此校准时，不把单条查询召回率当总体召回率。默认PQ配置保持已讨论的可运行参数；高召回超预算点不出证、不预测。曲线使用少量倍增probe点，不做密集整数扫描。

VLAH、ANNProof、Ours 保持原默认约95%及全部既有数据。每次覆盖外层work9.xlsx前，逐单元格检查非V3DB区域（值、样式、批注）与work9_before_single.xlsx完全一致。旧的work9_resource_matched.py已经禁用，不能重新运行。

当前进程先测缺少完整证明的GloVe默认点，再复用其他三个数据集的固定样本，随后按单次口径填写V3DB曲线、K、规模表。配置重复时共用同一次真实出证。完整结果在records/及points/；进度看progress.json、runner.log；只有RUN_FINISHED.json表示计划完成。失败记录在runner_failure.json，禁止把启动当成完成。

启动/断点续跑（内层项目目录）：

```powershell
wsl --exec /root/.venvs/v3db-work9/bin/python scripts/work9_v3db_single.py start
```
