"""Summarize completed Ours measurements and actual comparison constraints."""
import json
from fill_work9_ours import ROOT,STUDY,SOURCE,TEMPLATE,OUTPUT,runs,size_evaluation
from fill_work9 import sha


def main():
    measurements=runs()
    selected=[r for r in measurements if r['name']=='sift1m-overall']
    selected+=[r for n in [100000,500000,1000000,5000000,10000000] if (r:=size_evaluation(measurements,n))]
    lines=['# VeriANN 全部可组合优化：work9 实测', '',
           '实际启用：SQ4 top-1、SQ4→Delta4→6→raw、Direct-content Expansion-Centric Merkle、'
           'BFS refinement ordering、共享路径multiproof、隐式canonical proof坐标、预打包SQ4/Delta、稀疏查询状态。', '',
           'M=8、efConstruction=100，复用相应 ANNProof 的同一 HNSW 缓存；E 是 expanded-node 预算。'
           '旧研究停止的 progressive adjacency 和互斥 MAC/ordering/search ablation 不叠加。', '',
           'Query Time 包含搜索、精确候选top-k和实际VO生成/序列化；Verify包含解码、认证、遍历重放和渐进top-k检查。'
           'VO使用十进制KB；未包含网络和区块链操作。正式100条查询、3次重复；独立100条查询用于初始95%门槛校准。'
           '500K和10M初始测试召回偏低，另在固定正式查询集上补测E网格并选择最接近95%的实测点；这两点属于工作点匹配，不是独立泛化评估。', '',
           '| 实验 | N | E | 实测 Recall@10 % | Query ms | Verify ms | VO KB | Peak RSS GiB |',
           '|---|---:|---:|---:|---:|---:|---:|---:|']
    for r in sorted(selected,key=lambda r:(r['name']!='sift1m-overall',r['base'])):
        summary=r['summary'][0]
        name='SIFT1M 总体' if r['name']=='sift1m-overall' else 'BIGANN 前缀'
        lines.append(f"| {name} | {r['base']:,} | {r['ef']} | {r['recall_percent']:.2f} | "
                     f"{r['query_ms']:.3f} | {r['verify_ms']:.3f} | {r['vo_kb']:.3f} | {float(summary['peak_rss_mib'])/1024:.3f} |")
    overall=next((r for r in selected if r['name']=='sift1m-overall'),None)
    if overall:
        baseline=json.loads((ROOT/'experiments/work9-20261015/workbook_audit.json').read_text())
        cells={c['cell']:c['value'] for c in baseline['cells']}
        lines+=['', '## SIFT1M 总体与表中 ANNProof', '',
                f"Ours Recall@10={overall['recall_percent']:.2f}%，ANNProof={cells['E10']:.2f}%。",
                f"VO 减少 {(1-overall['vo_kb']/cells['G10'])*100:.2f}%；"
                f"验证时间减少 {(1-overall['verify_ms']/cells['H10'])*100:.2f}%；"
                f"查询计算时间增加 {(overall['query_ms']/cells['F10']-1)*100:.2f}%。", '',
                '这些是本机当前实现的结果，时间比较还受不同时段运行的波动影响；不能据此宣称所有指标均更优。']
    audit=json.loads((STUDY/'workbook_audit.json').read_text())
    lines+=['', '## 核验与限制', '',
            f"Ours 实测单元格：{audit['ours_numeric_cells']}/103；保留 baseline 原206个数值。",
            f"通过验证的逐条测量：{sum(r['samples'] for r in measurements):,}（包含校准；重复查询不算新查询）。",
            '校准召回达到95%不保证独立测试集恰好95%；500K、10M的原始正式Recall分别92.9%、91.2%，原记录保留；表格用另行补测的约95%工作点，未插值、删查询或改写测量。',
            '现有baseline只用20条查询校准，部分规模点偏离更大；全表不是严格等召回性能比较，baseline为此前披露的链下算法重实现。',
            '原版回归：原SIFT100K/M16/E64缓存，Recall@10=99.2%；新旧轨迹、答案、量化下界、认证叶一致性检查通过。',
            '与旧121.255 KiB计费结果不同，本次VO包含真实header、结果、记录位置与计数等wire开销。', '',
            f'工作簿：`{SOURCE}`',f'原表备份：`{TEMPLATE}`',
            f'工作簿 SHA256：`{sha(SOURCE)}`',f'完成版 SHA256：`{sha(OUTPUT)}`', '']
    (STUDY/'RESULTS_CN.md').write_text('\n'.join(lines),encoding='utf-8')
    print(json.dumps({'ours_filled':audit['ours_numeric_cells'],'configurations':len(measurements),
                      'measurements':sum(r['samples'] for r in measurements),'report':str(STUDY/'RESULTS_CN.md')}))


if __name__=='__main__':main()
