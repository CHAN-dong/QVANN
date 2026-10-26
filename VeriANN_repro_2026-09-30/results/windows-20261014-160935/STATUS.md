# 本机复现成功：2026-10-14

Windows 入口执行结束，退出码 0。三个核心实验均从当前 C++ 源码重新编译，
在独立工作目录实际运行；本目录 CSV 是本次生成结果。

## 环境

- Windows + WSL2，发行版 Ubuntu 26.04.1 LTS。
- Linux kernel：6.18.54.1-microsoft-standard-WSL2。
- GCC/g++ 15.2.0，Python 3.14.4，OpenSSL 3.5.5。
- OMP_NUM_THREADS=1，OPENBLAS_NUM_THREADS=1。
- 使用包内修改版 Faiss/LightGBM 和 runtime 库，以及原缓存 HNSW 索引。
- 当前 Ubuntu 非交互命令以 root 运行，未创建普通 Linux 用户。

## 实际结果

| 实验 | 本次结果 |
|---|---|
| SQ4 E=64 | Recall@10=0.992，expanded=64，disclosed=811.86，VO=76.0113 KiB |
| Direct-content ER | proof=56.3922 KiB，total VO=132.3410 KiB |
| HNSW-BFS refinement | refinement proof=27.6628 KiB，total VO=121.2554 KiB |

`compare_key_results.py --actual <本目录>` 对三份汇总 CSV 均输出 PASS，
忽略计时字段后的最大数值误差为 0。
两份 per-query CSV 的 SHA256 与 `results/key/` 对应文件完全一致。
Merkle 实验中的正常证明及翻转 leaf bit 篡改自检均通过，否则程序会非零退出。

完整日志见 `build.log`、`run.log`、`compare.log`；三个 experiment.log
是干净的实验进程输出。编译存在原研究源码的未使用参数、缩进及 OpenMP pragma
警告，但无编译错误。WSL 的 localhost 代理提示没有阻止下载、编译或实验。

## 适配内容与重复运行

- Windows PowerShell 入口使用正斜杠将 Windows 路径传入 wslpath，
  避免反斜杠被 WSL 命令解析吞掉。
- 独立临时工作目录运行，独立结果目录保存日志及 CSV。
- 比较脚本支持 `--actual` 并检查全部三个汇总文件。
- 搜索、量化、认证算法未改变。

在项目根目录运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_key_experiments_windows.ps1
```

本次仅覆盖三组打包核心实验，未运行其余研究分支。
