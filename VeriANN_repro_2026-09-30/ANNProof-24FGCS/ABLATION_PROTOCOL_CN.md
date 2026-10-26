# ANNProof 认证优化消融

目的：量化认证路径共享和 Merkle 分片的贡献。保留完整版本作为主表 baseline，消融结果单列，不预设性能排序。

| 标记 | proof | shard-size | 含义 |
|---|---|---|---|
| Full | multiproof | 10000 | 当前项目实现 |
| No-sharing | paths | 10000 | 每节点独立认证路径，保留重复路径字节 |
| No-sharding | multiproof | N | 整库一棵树，对应 ANNProof-U |
| No-sharing/no-sharding | paths | N | 同时关闭两项优化 |

这里 multiproof 是当前重实现采用的优化，不能未经核对称为作者默认设置。取消分片不保证每个工作点都更慢或更大；按实测报告。

## 固定条件

- 四数据集，每个配置查询 ID 100–119，每条实际重复 3 次；查询及验证单线程。
- 同一数据版本、同一 HNSW 缓存和图参数 M/efConstruction、同一 k/efSearch、同一浮点距离和停止规则。
- 仅改变上述认证参数；比较逐查询答案、召回率、visited、expanded，必须一致。
- 不减少密码学安全参数，不发送无用记录或人为延时，不更换编译优化等级。
- 顺序运行，避开 V3DB 正式计时；不把并发资源竞争造成的变慢当算法结果。
- 每个工作点完整 60 次后才汇总，不用部分结果代替均值。

## 指标与核验

报告实际完整 VO 字节、record_bytes、local_proof_bytes、搜索时间、VO 生成时间、认证时间、重放时间、验证总时间、server_total_ms、召回率，以及可信根注册表大小。可信根离线预置和链上/网络成本的口径必须一致。

四个配置均要求真实 VO 解码与验证、正常样本通过、篡改样本被拒绝，query0.vo 文件字节数匹配记录。主表保留 Full；独立消融表报告绝对值及相对 Full 的比值，允许比值小于 1。

## 已有运行接口

从项目根目录调用 `python ANNProof-24FGCS/run.py`。公共参数使用原测量的 dataset/base/M/ef-construction/k/ef-search，增加 `--queries 20 --query-offset 100 --repeats 3 --build-threads 1 --skip-build`。四次分别指定上表的 `--proof`、`--shard-size` 和互不相同且为空的 `--output`。

这里 N 表示实际 base 数量。不能使用默认 efSearch=50 的结果替换原表高召回配置。

状态：已检查当前源码支持这些开关；本协议未启动新计时实验，避免干扰正在运行的 V3DB 单线程实验。
