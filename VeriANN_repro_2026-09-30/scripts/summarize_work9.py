"""Write a compact report from validated work9 runs; never estimates missing runs."""
import json
from pathlib import Path
from fill_work9 import STUDY, load_runs, sha, TEMPLATE, OUTPUT, ORIGINAL


def main():
    runs=load_runs()
    evaluation=[r for r in runs if r['name'].endswith('-evaluation')]
    lines=['# work9 实测结果', '',
           '主表 Query Time 包含搜索和 VO 生成；VO 使用十进制 KB。',
           '以下为本机链下算法重实现的结果，未测区块链网络与共识，不能当作论文原程序严格数值复现。', '',
           '## BIGANN/SIFT1B 前缀规模实验', '',
           '参数由独立的20条校准查询挑选；正式100条查询重复3次。'
           '测试召回率可能明显偏离95%校准门槛，以下同时展示实际召回，不代表严格等召回对比。', '',
           '| 方法 | N | ef | probes | Recall@10 % | Query ms | Verify ms | VO KB | Peak RSS GiB |',
           '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for r in sorted(evaluation,key=lambda x:(x['base'],x['method'])):
        s=r['summary'][0]
        lines.append(f"| {r['method']} | {r['base']:,} | {r['ef']} | {r['probes']} | "
                     f"{r['recall_percent']:.2f} | {r['query_ms']:.3f} | {r['verify_ms']:.3f} | "
                     f"{r['vo_kb']:.3f} | {float(s['peak_rss_mib'])/1024:.3f} |")
    lines.extend(['', '## 审核记录', ''])
    audit=json.loads((STUDY/'workbook_audit.json').read_text())
    lines.append(f"实测数值单元格：{audit['completed_numeric_cells']}/{audit['total_target_cells']}。")
    lines.append(f"通过验证的逐条查询测量：{sum(r['samples'] for r in runs):,}；"
                 '包括独立校准，不把三次重复算成三倍不同查询。')
    lines.extend(['', '原始逐条 CSV、VO 文件、诚实/篡改测试和环境配置保留在每个实验结果目录。',
                  'Excel 中“实验说明”“实验配置与实测召回”“单元格来源”“资源与尾延迟”提供口径与证据。', '',
                  f'原表备份 SHA256：`{sha(TEMPLATE)}`',
                  f'完成版 SHA256：`{sha(OUTPUT)}`',
                  f'原文件已更新为完成版：{sha(ORIGINAL)==sha(OUTPUT)}', ''])
    (STUDY/'RESULTS.md').write_text('\n'.join(lines),encoding='utf-8')
    print(json.dumps({'filled':audit['completed_numeric_cells'],'runs':len(runs),
                      'measurements':sum(r['samples'] for r in runs),'report':str(STUDY/'RESULTS.md')}))


if __name__=='__main__':
    main()
