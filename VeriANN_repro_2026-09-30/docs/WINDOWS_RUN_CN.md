# Windows 本机复现

此项目使用 Linux ELF 程序及修改版 Faiss 的 `.so` 库。Windows 入口通过
WSL/Ubuntu 运行同一份 C++ 实验，不改变搜索或认证算法。

2026-10-14 已在本机 WSL2 / Ubuntu 26.04.1、GCC 15.2.0 下重新编译并
跑通三个核心实验，三份汇总比较均 PASS，非计时最大误差为 0。
本次结果及日志见 `results/windows-20261014-160935/`。

## 环境准备

管理员 PowerShell 安装 WSL（提示重启时先重启；首次启动 Ubuntu 完成用户初始化）：

```powershell
wsl --install -d Ubuntu --no-launch
wsl -d Ubuntu
```

Ubuntu 内安装编译依赖：

```bash
sudo apt-get update
sudo apt-get install -y g++ make python3 libssl-dev libopenblas-dev liblapack-dev libgomp1 gfortran
```

## 运行

在项目根目录的 PowerShell 执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_key_experiments_windows.ps1
```

可用 `-Distribution Ubuntu-24.04` 指定已安装的发行版。默认重新编译三个核心实验；
只有确认现有 Linux 程序与环境兼容时才使用 `-SkipBuild`。

每次创建 `results/windows-年月日-时分秒/`，保存环境、编译、运行、比较日志和五份 CSV。
实验在独立临时工作目录运行，失败不会使用 `build/` 内旧 CSV 进行比较。
全部执行成功且三份汇总 CSV 比较通过后才打印最终 `PASS`。
计时字段不比较，其余数值绝对容差为 `1e-4`。

预期搜索结果：`sq4 E=64 recall=0.992`、`exp=64`、`disc=811.86`。
Direct ER 预期总 VO 约 `132.341 KiB`；HNSW-BFS 约 `121.255 KiB`。
Merkle 实验还执行正常证明及翻转 leaf bit 的篡改自检，失败会以非零状态退出。

当前改动涉及运行/比较脚本，原始 SHA256 清单中的对应脚本校验会因此变化。
