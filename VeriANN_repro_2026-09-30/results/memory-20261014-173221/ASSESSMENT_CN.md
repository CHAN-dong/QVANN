# 10M / 100M 内存评估（2026-10-14）

本机安装 4 x 32 GiB = 128 GiB 物理内存。Windows 查询显示系统可见约
127.68 GiB，采样时物理可用约 109.64 GiB。WSL MemTotal=65,680,328 KiB
（62.64 GiB），MemAvailable=64,460,056 KiB（61.47 GiB），swap=16 GiB。
swap 不算实验可用物理内存。D 盘可用约 630 GiB。

## 实测

使用 `/usr/bin/time` 的 maximum resident set size 单独测量现有 100K 三组
核心程序，单线程、原缓存索引，各自完整运行，退出码均为 0。
没有运行 10M/100M，也没有下载大规模数据。

| 实验 | 实测 100K 峰值 RSS MiB | 10M 预计峰值 GiB | 100M 预计峰值 GiB |
|---|---:|---:|---:|
| E-budget search | 118.88 | 12–16 | 125–150 |
| 三种 Merkle 认证对比 | 200.12 | 20–26 | 200–230 |
| 三种 refinement ordering | 161.82 | 17–23 | 165–190 |

预测值是依据当前实现的分配结构及实测交叉检查给出的规划范围，不是大规模实测，
不是严格上界。缓存加载和新建图索引的峰值会不同；分配器行为也会影响峰值。
三组实验顺序运行，因此不把三行峰值相加。

## 估算依据

- 向量：128 float32 = 512 B/vector，10M=4.77 GiB，100M=47.68 GiB。
- HNSW M=16：level-0 32 邻居，另含上层、offsets、levels 等；
  包内 100K 索引 65,633,274 B，约 656 B/vector。
  因此含向量的索引约 6.11 GiB / 61.13 GiB。
- `read_fvecs` 逐行 resize，原始向量数组存在 capacity 富余及重分配瞬时副本。
  原始 base 直到 HNSW 加载/构建完成后才释放。
- ESQ 和 AuthSQ 各按每维 1 byte 保存 6-bit code，合计 256 B/vector，
  认证实验约 2.38 GiB / 23.84 GiB，尚未做物理位压缩。
- `MerkleTree` 保存完整树，leaf 补齐到 next power of two。
  理想树大小为 `32 * (2 * nextpow2(leaves) - 1)` B。
  `vector.resize` 还可能保留超过有效叶子数的 capacity。
- 排序实验同时保留一棵 routing 树及三套 Delta/Raw 树，总共七棵树；
  不计 capacity 的理想树大小在 10M 约 7 GiB，100M 约 56 GiB。
  三种 ER 对比树的理想值为约 8 GiB / 96 GiB；实际 capacity 会更高。
- 另有 order/position、SQ4 排序临时缓存、全数据集 ground-truth 距离数组、
  查询 bitmap 和距离数组、multiproof 扫描缓冲等。

## 判断及下一步

10M：当前 62.6 GiB WSL 上限足以支持预计 20–26 GiB 的最重核心实验。
规划预留 30–35 GiB，三组顺序运行。

100M：当前代码的完整实验不适合本机；即使扩大 WSL 上限也不足。
只包含 float32 的 HNSW 索引就约 61 GiB，接近整个 WSL 内存上限。
建议该规模现有完整实现使用至少 256 GiB RAM，再实测评估余量。

大规模可通过流式/bvecs 读取、消除 base 重复副本、共享压缩 SQ code、
一次只构建一个对比方案的树、mmap/外存树和稀疏 multiproof 等改造降低内存。
此外当前主程序固定 100K 参数及缓存路径，不能仅换数据文件就运行 10M。
BIGANN 的 bvecs 也需要输入适配；大规模需独立索引、正确 ground truth 和快照。
代码对每条查询扫描全数据集计算 ground truth、分配全 N 数组，proof 也扫描整层树，
因此内存可行不代表实验用时一定合理。
