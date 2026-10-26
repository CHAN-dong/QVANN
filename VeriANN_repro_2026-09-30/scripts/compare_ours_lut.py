"""Sequential ABBA comparison at the exact same SIFT1M E=128 work point."""
import csv
from datetime import datetime
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys
from prepare_ours_lut import ROOT,STUDY


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def read_rows(folder):
    with (folder/'per_query.csv').open() as f:return list(csv.DictReader(f))


def main():
    STUDY.mkdir(parents=True,exist_ok=True)
    original_book=ROOT.parent/'work9.xlsx';before=sha(original_book)
    # Check the original-index regression before starting the timing experiment.
    gold=ROOT/'results/ours-development/reference-final'
    reference=ROOT/'results/ours-lut-20261015/reference'
    assert (reference/'PASS.txt').exists()
    assert sha(reference/'query0.vo')==sha(gold/'query0.vo')
    assert (reference/'trusted_roots.txt').read_bytes()==(gold/'trusted_roots.txt').read_bytes()
    records=[]
    for label,variant in [('scalar-a','scalar'),('lut-a','lut'),('lut-b','lut'),('scalar-b','scalar')]:
        output=ROOT/'results/ours-lut-20261015'/label
        if not (output/'PASS.txt').exists():
            if output.exists() and any(output.iterdir()):raise RuntimeError('nonempty failed run; inspect before retry: '+str(output))
            cmd=[sys.executable,str(ROOT/'scripts/run_ours_benchmark.py'),'--dataset',str(ROOT/'dataset/sift1m'),
                 '--output',str(output),'--base','1000000','--queries','100','--query-offset','100',
                 '--repeats','3','--M','8','--k','10','--E','128','--reference-check','--skip-build']
            if variant=='lut':cmd+=['--source',str(ROOT/'build/veriann_lut_benchmark.cpp'),'--binary',str(ROOT/'build/veriann_lut_benchmark')]
            print(json.dumps({'action':'start','label':label,'time':datetime.now().isoformat()}),flush=True)
            with (STUDY/(label+'.console.log')).open('w') as log:
                process=subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            if process.returncode:raise RuntimeError((STUDY/(label+'.console.log')).read_text()[-10000:])
        rows=read_rows(output);assert len(rows)==300 and all(r['verified']=='1' for r in rows)
        metric={key:statistics.fmean(float(r[key]) for r in rows) for key in
                ['search_ms','vo_generation_ms','verification_ms','recall_at_k','vo_bytes']}
        metric['query_ms']=metric['search_ms']+metric['vo_generation_ms']
        records.append({'label':label,'variant':variant,'output':str(output),'metrics':metric})
        print(json.dumps({'action':'passed','label':label,'metrics':metric}),flush=True)
    base=Path(records[0]['output']);base_rows=read_rows(base)
    stable=[name for name in base_rows[0] if name!='method' and not name.endswith('_ms')]
    for record in records:
        folder=Path(record['output']);rows=read_rows(folder)
        for old,new in zip(base_rows,rows):assert all(old[key]==new[key] for key in stable)
        assert sha(folder/'query0.vo')==sha(base/'query0.vo')
        assert (folder/'trusted_roots.txt').read_bytes()==(base/'trusted_roots.txt').read_bytes()
        assert len(list(csv.DictReader((folder/'reference_checks.csv').open())))==10
    aggregate={variant:{key:statistics.fmean(r['metrics'][key] for r in records if r['variant']==variant)
                        for key in records[0]['metrics']} for variant in ['scalar','lut']}
    reduction={key:100*(1-aggregate['lut'][key]/aggregate['scalar'][key]) for key in
               ['search_ms','vo_generation_ms','query_ms','verification_ms']}
    assert sha(original_book)==before,'the comparison must not modify the existing workbook'
    result={'status':'PASS','order':'scalar, LUT, LUT, scalar','queries':100,'query_offset':100,'repeats_per_batch':3,
            'k':10,'E':128,'M':8,'query_threads':1,'measurements':1200,
            'identical_non_timing_columns':stable,'query0_vo_identical':True,'roots_identical':True,
            'original_workbook_unchanged_sha256':before,'runs':records,'aggregate':aggregate,'reduction_percent':reduction}
    (STUDY/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    lines=['# 查询查表优化：SIFT1M 对照实验','',
           '固定同一图、查询IDs100–199、k=10、E=128、单线程。按原版、查表、查表、原版顺序运行，每批100条查询重复3次。',
           '查表准备成本在服务器搜索和客户端验证计时内；没有更改VO协议、召回、搜索预算或舍入规则。','',
           '| 指标 | 原版 ms | 查表版 ms | 减少 |','|---|---:|---:|---:|']
    for key,title in [('search_ms','搜索/精排'),('vo_generation_ms','VO生成'),('query_ms','查询合计（含VO）'),('verification_ms','客户端验证')]:
        lines.append(f"| {title} | {aggregate['scalar'][key]:.3f} | {aggregate['lut'][key]:.3f} | {reduction[key]:.1f}% |")
    lines+=['',f"Recall@10={aggregate['lut']['recall_at_k']*100:.2f}%；VO={aggregate['lut']['vo_bytes']/1000:.3f} KB。",
            '逐条访问数量、展开数量、质量指标、VO大小等非计时字段完全一致，样本VO字节和三个Merkle根完全一致。',
            '额外原SIFT100K缓存回归仍为99.2%召回，量化分数、上下界、轨迹和认证叶一致性检查通过。',
            '这些是SIFT1M、E128单机工作点的对照结果；未据此外推其他数据规模或查询预算。现有work9.xlsx保留不变。','']
    (STUDY/'RESULTS_CN.md').write_text('\n'.join(lines),encoding='utf-8')
    print(json.dumps({'status':'PASS','aggregate':aggregate,'reduction_percent':reduction}))


if __name__=='__main__':main()
