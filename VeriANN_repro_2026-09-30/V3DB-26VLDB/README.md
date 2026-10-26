# V3DB 作者实现复现

本目录独立复现论文 `V3DB: Audit-on-Demand Zero-Knowledge Proofs for Verifiable Vector Search over Committed Snapshots`，不改变现有 VeriANN、ANNProof、VLAH 实现或工作簿。

作者代码：https://github.com/Relaxed-System-Lab/V3DB

固定 commit：`a82ac732828591bd49b83d8f31c56fa4bd1cd9b2`。`upstream/` 保留作者仓库，未修改其受版本控制的源码。新增的构建、运行、结果汇总代码在外层 `scripts/`。

## 已执行的测试

- 编译作者 Rust/PyO3 扩展，使用原始 `Cargo.lock`、release 模式。
- 小规模 multiset 与 circuit-only：均包含 Merkle 承诺检查，实际生成、压缩并内部验证 Plonky2 证明。
- 论文 Table 8 basic 参数的两种实现：`(N,D,M,K,n_list,n_probe,k)=(8192,128,8,16,256,16,64)`。电路构建前门数分别为 21,198 和 101,016，与论文一致。
- 真实 SIFT：现有 SIFT 数据的前 4,096 条 base、前 10 条 query；作者训练、重平衡、检索实现；第一条 query 实际生成并内部验证带 Merkle 的 multiset 证明。仅证明第一条，其余用于检索指标。
- 负向测试：保持原列表根不变，篡改已打开的一条 PQ code，作者证明路径以 witness 不一致错误拒绝。这是生成阶段拒绝测试，不是传输证明字节的篡改测试。

实际结果见 [RESULTS.md](RESULTS.md)、[汇总 CSV](results/summary.csv)、[校验结果](results/validation.json)。每项原始返回值在 `results/*.json`，完整输出在 `logs/*.log`。

## 运行

本机使用 WSL Ubuntu，Python 3.12.15，Rust nightly-2026-10-16，CPU Faiss。Python 包版本固定在 `requirements.lock`；Rust 依赖固定在作者 `Cargo.lock`。环境默认位于 WSL `/root/.venvs/v3db-repro`，构建缓存位于 `/root/.cache/v3db-target`，避免在 Windows 挂载盘中产生大量编译文件。

从本目录在 Windows PowerShell 运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\run_windows.ps1 -Suite all
# 需要重建时：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\run_windows.ps1 -Build -Suite all
```

或在 WSL 进入本目录：

```bash
bash scripts/build.sh
bash scripts/run.sh all
# 只做小规模正向、负向测试：
bash scripts/run.sh smoke
```

构建脚本要求 WSL 已有 GCC、git、curl、rustup 和 uv。本机已安装 rustup 与 uv；新机器可按二者官方安装方法准备，再运行 build.sh。可用 `V3DB_VENV` 和 `V3DB_TARGET_DIR` 更换环境/缓存位置。

每项测试在独立进程中顺序执行，使用 4 个 Rayon 线程、1 个 OpenMP/OpenBLAS 线程。重新执行会覆盖同名结果与日志。合成数据使用作者默认随机生成器，不固定种子；真实 SIFT 训练种子为 0。

## 测量边界与源码观察

1. 作者 Python 接口返回 `(build_time, prove_time, verify_time, proof_size, memory_used, num_gates)`。时间单位是秒，大小/内存单位是字节；本目录明确转换列名与显示单位。
2. `prove_time` 包含作者内存采样器启动/结束等待；不含电路 build、证明压缩及 Python 数据准备。证明大小是压缩证明大小；`verify_time` 验证的是未压缩内存证明。不能直接把它解释为客户端接收压缩证明后的完整处理时间。
3. 内存是作者在 prove 阶段每 10 ms 采样得到的进程 RSS 峰值，不覆盖完整训练、建树或 build 阶段；门数是 `builder.build()` 前统计，不是 padding 后域大小。
4. 本次 basic 为每种方法一次运行，用于跑通和核对门数，不是论文五次重复的均值/置信区间。本机线程、硬件和分配器与论文不同，时间和内存不应直接等同论文数值。尚未执行 large、完整 SIFT1M/GIST1M/MS MARCO 或全部参数扫描。
5. SIFT 子集 ground truth 是在同一 4,096 条 base 上重新精确计算的集合 Recall@10；不使用 SIFT100K 的旧 ground truth，也不冒充论文 Table 6 指标。这里使用原始 SIFT 整数坐标和作者训练路径，未复制论文完整 16-bit 缩放实验。训练应用每簇容量 128；证明函数按实际最大簇大小取二次幂 padding。
6. **默认没有开启零知识隐藏。** 作者 `src/prelude.rs::make_builder()` 使用 `CircuitConfig::standard_recursion_config()`，锁定版本 Plonky2 1.1.0 中该配置 `zero_knowledge=false`。本次忠实运行默认配置，不宣称隐私保证已经复现。
7. **多重集挑战的安全绑定仍需进一步核查。** `src/merkle_ver/set_based_proof.rs` 在宿主端仅从 query 生成 `fs_hash`，再作为虚拟 witness 输入 gadget；该路径没有在电路内重新约束此派生关系。功能测试通过不能视为对恶意 prover 的完整安全审计。
8. 作者 Python API 不导出证明字节、验证密钥或公开输入供独立进程验证；本次采用作者内部 `data.verify(proof)`。SIFT 的 fast/proof 路径 top-k 对比发生在 Python 返回值层面，不能替代从证明公开输入独立提取 top-k 的检查。

上述观察保留原实现，并与测量结果一起记录；本轮未擅自修订作者协议或隐藏这些差异。
