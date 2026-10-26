"""Report only complete, verified 20x3 operating points, without interpolation."""
import csv
import json
import statistics
from work9_nytimes import STUDY,N,D,sha,write

records=[]
for method in ['ANNProof','VLAH','Ours']:
    job=json.loads((STUDY/f'evaluation-nytimes256-{method}-curve-{N}.json').read_text())
    choice=job['selection'][7]
    assert choice['target']==.95
    from pathlib import Path
    source=Path(job['output'])/'per_query.csv'
    rows=[r for r in csv.DictReader(source.open()) if int(r['k'])==10 and int(r['ef_search'])==choice['ef'] and int(r['probes'])==choice['probes']]
    assert len(rows)==60 and all(r['verified']=='1' for r in rows)
    assert {(int(r['query']),int(r['repeat'])) for r in rows}=={(q,t) for q in range(100,120) for t in range(3)}
    mean=lambda k:statistics.fmean(float(r[k]) for r in rows)
    records.append(dict(method=method,target=.95,recall=mean('recall_at_k'),
        query_ms=statistics.fmean(float(r['search_ms'])+float(r['vo_generation_ms']) for r in rows),
        verify_ms=mean('verification_ms'),vo_kb=mean('vo_bytes')/1000,
        parameter=choice['ef'],probes=choice['probes'],samples=60,source=str(source),source_sha256=sha(source)))
write(STUDY/'primary_summary.json',dict(N=N,d=D,query_ids=[100,119],repeats=3,threads=1,records=records))
lines=['# NYTimes-256 主实验检查点','',
    f'清洗后 N={N:,}，d={D}，angular 经单位归一化转换为平方 L2。k=10；20条不同查询×3次，单线程。',
    '','| 方法 | 实际 Recall@10 | Query ms | Verify ms | VO KB |',
    '|---|---:|---:|---:|---:|']
for r in records:lines.append(f"| {r['method']} | {r['recall']:.1%} | {r['query_ms']:.3f} | {r['verify_ms']:.3f} | {r['vo_kb']:.3f} |")
lines+=['','以上是统一95%目标对应的离散实测档位，实际召回率不改写成目标值；同查询集校准与评测，不是独立留出估计。Query 包含搜索和真实VO生成；Verify 包含解码、认证和重放；KB=bytes/1000；不含网络及区块链。',
    '', 'ANNProof、VLAH、Ours的总体/召回/k/100K规模表及六行受控消融均已完成并回填。超过实际容量的500K/1M/5M/10M标为不适用。',
    '', 'V3DB尚未全部完成：高召回配置已实际触及50 GiB进程地址空间限制；较低召回档位继续单线程证明，不填写未完成的耗时均值。最新状态以工作簿和原始result.json为准。',
    '', 'GIST1M记录保留归档并移出新主表；替换是用户在查看原实验后提出的新增评测，不能称为预先选定数据集上的无偏优势结论。',
    '', '逐组来源及SHA256见primary_summary.json；原始主实验共2700次测量，六行受控消融共360次，均真实生成并验证VO。核验见validation.json。']
(STUDY/'RESULTS.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(json.dumps(records,indent=2))
