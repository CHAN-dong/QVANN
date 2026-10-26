# VeriANN: IVF Threshold-Based Batch Verification

这是文档中第一阶段实验的简洁 C++17 复现。程序对同一批 IVF posting lists 比较：

- **Simple IVF VO**：返回所有 `Visited Objects`；
- **Threshold VO**：查询得到 top-k 阈值后，用 Ball Tree 安全下界 `LB=max(0, ||q-m||-radius)` 剪枝，只返回 `Opened Objects + Frontier Nodes`。

这里复现的是几何剪枝与 VO 数量模拟，不包含哈希、签名和 multiproof 的密码学字节开销。树在原坐标中构造；由于每簇 residual 只是统一平移，这与 residual-space Ball Tree 完全等价。

## 构建和运行

```bash
cd VeriANN
make
./veriann --queries 100 --leaf-size 32
```

也可使用随附的 `CMakeLists.txt` 在装有 CMake 的环境中构建。

默认使用项目中的 SIFT1M、IVF100 assignment 文件，并测试 `nprobe=1,2,4,8,16,32,64,100`。从其他工作目录运行时请显式传路径：

```bash
./veriann --dataset /path/to/sift --assignments /path/to/clusterid.tsv \
  --nlist 100 --queries 100 --k 10 --leaf-size 32 \
  --nprobes 1,2,4,8,16,32,64,100 --output results.csv
```

`results.csv` 同时给出 Recall@k、扫描/打开/摘要节点数量、原始向量减少率和总 VO item 减少率。Recall@k 定义为返回 top-k 与 SIFT ground-truth top-k 的交集比例。

## 安全量化证书实验

```bash
make quantization_experiment
./quantization_experiment --queries 50 --k 100 --bits 4,6,8
```

该实验使用认证误差下界 `||q-x_hat||-epsilon`，统计只需量化证书和必须返回完整向量的对象比例，并估算 payload bytes。

## Residual PQ 与渐进式量化消融

```bash
make residual_pq_experiment
./residual_pq_experiment --queries 50 --k 100 --train-size 20000 --iterations 10
```

程序比较单层 8-byte RPQ、固定 16-byte 两层 RPQ，以及按需发送第二层的渐进式 8→16-byte 证书。结果和分析见 `RESIDUAL_PQ_RESULTS.md`。

## 当前主方案：SQ4 + block authentication

```bash
make block_auth_experiment
./block_auth_experiment --queries 50 --k 100 --block-sizes 64,128,256,512,1024
```

程序构建真实 SHA-256 量化块、exact-vector 子树、cluster roots 和全局 root，执行篡改自检，并测量块内/全局 multiproof 字节。当前建议 block size 为 256；详见 `BLOCK_AUTH_RESULTS.md`。

## VO-aware SQ template selection

```bash
make vo_aware_quantization_experiment
./vo_aware_quantization_experiment --test-queries 50 --train-queries 50 --k 100
```

测试集使用 query 0–49，训练使用 50–99，并在训练内部选择收缩正则。程序按 cluster 从 SQ2/SQ3/SQ4 中选择实际 VO 最小的模板；独立测试结果见 `VO_AWARE_RESULTS.md`。

## 已知阈值下的认证批量排除

```bash
make batch_pruning_experiment
./batch_pruning_experiment --base 100000 --queries 50 --k 100 \
  --nprobes 8,16,32,64,100 --leaf-sizes 16,32,64,128,256
```

该实验在 posting list 内组合认证 Ball Tree、SQ4 leaf records 和按需 exact vectors，并把树摘要开销计入 VO。结果表明批量排除只在很高 `nprobe` 下产生净收益；详见 `BATCH_PRUNING_RESULTS.md`。

半径感知的相似向量可变块实验：

```bash
make adaptive_batch_experiment
./adaptive_batch_experiment --base 100000 --queries 50 --k 100 \
  --max-block 16 --gammas 1,1.05,1.1,1.15,1.2,1.3,1.5,1000000
```

Query-aware VO-cost 动态规划分块：

```bash
make query_aware_batch_experiment
./query_aware_batch_experiment --base 100000 --test-queries 50 \
  --train-queries 200 --k 100 --nprobes 8,16,32,64,100 \
  --max-block 16 --output query_aware_batch_results_sift100k_k100_train200.csv
```

该实验严格使用独立训练/测试 query，并允许局部退回不分块的 SQ4 records。结果见 `QUERY_AWARE_BATCH_RESULTS.md`。

## 当前结构主方案：认证微簇 + 两级查询协议

```bash
make workload_hybrid_vo_experiment microcluster_routing_experiment
./workload_hybrid_vo_experiment --dataset ../dataset/sift --base 100000 \
  --train-queries 200 --test-queries 100 --k 100 \
  --nlist 65536 --nprobe 416 \
  --coarse-nlist 1024 --coarse-nprobe 224 --iters 6 --repeats 3 \
  --output workload_microcluster_twostage_final_ivf65536_np416_c1024_cp224_20260826.csv

./microcluster_routing_experiment --dataset ../dataset/sift --base 100000 \
  --queries 100 --k 100 --fine 65536 --probe 416 --coarse 1024 \
  --coarse-probes 16,32,64,128,192,224,256 --iters 6 --repeats 3 \
  --output microcluster_routing_sift100k_k100.csv
```

匹配旧 recall 的完整两级协议为 124.15 KiB/query、Recall@100=0.9942、9.81 ms/query。完整设计、消融、安全边界和后续方向见 `MICROCLUSTER_QUERY_PROTOCOL_RESULTS_20260825.md`。
