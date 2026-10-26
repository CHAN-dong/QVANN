# QVANN

QVANN: A Quantization-Driven Authenticated Index for Efficient Verifiable ANN Search.

本仓库是 2026-10-26 整理的研究代码与结果快照。主项目保留在
[`VeriANN_repro_2026-09-30/`](VeriANN_repro_2026-09-30/README.md)，其中 VeriANN 是历史项目名称。

## 内容

- `VeriANN_repro_2026-09-30/src/`：QVANN 与各项消融实验的 C++ 源码。
- `VeriANN_repro_2026-09-30/scripts/`：构建、实验运行、数据准备与结果检查脚本。
- `VeriANN_repro_2026-09-30/experiments/`：实验说明、配置、汇总结果及 LaTeX 表格，包括量化、路由精度和 VO 分解实验。
- `VeriANN_repro_2026-09-30/docs/`：实现说明与研究记录。
- `VeriANN_repro_2026-09-30/third_party/`：已有第三方源码；上游许可文件随源码保留。
- `VeriANN_repro_2026-09-30/{ANNProof-24FGCS,Chen13-MR,VLAH-25TrustCom,V3DB-26VLDB}/`：基线实现与运行脚本。
- `papers/`、根目录表格与 `experiments/`：论文材料及项目外层的汇总文件。
- `PACKAGE_MANIFEST.tsv`：打包时所复制文件的 SHA-256 清单。

## 数据与运行

**本发布包不包含实验源数据。** 向量、查询集、ground truth、嵌入、索引缓存、序列化验证对象、逐查询样本、下载分片及编译二进制均已排除。CSV/JSON/XLSX 中保留的是研究汇总材料；历史实验说明可能引用未随包分发的文件。

原始项目 README 中“bundled / packaged / included datasets, indexes, libraries”的描述属于历史完整快照，**不适用于本次源码发布包**。现有脚本需要的数据与依赖必须在本地准备，不能直接假设已随仓库提供。

1. 参阅 [Windows / WSL 说明](VeriANN_repro_2026-09-30/docs/WINDOWS_RUN_CN.md) 和各实验 README，安装 C++17、OpenSSL、BLAS 等依赖。
2. 根据 `dataset/` 中保留的 README 以及 `scripts/download_extra_datasets.py`、`scripts/prepare_extra_datasets.py` 准备数据，并检查脚本的数据路径。下载脚本并不保证覆盖所有历史实验的数据来源。
3. 从源码构建对应的 Faiss/LightGBM 依赖，或在本机提供兼容库；本包未分发预编译运行库。然后在主项目目录运行 `bash scripts/build_key_experiments.sh`，按各实验说明生成所需索引并执行实验。

此次整理未修改算法，未重新运行完整实验。包内的汇总表是已有研究结果，不代表本次重新测得。
