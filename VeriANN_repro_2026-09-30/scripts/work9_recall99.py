"""Sequential measured recall curves with common workload recall thresholds."""
import csv
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
from datetime import datetime

ROOT = Path(__file__).resolve().parents[1]
STUDY = ROOT / 'experiments/work9-recall99-20261015'
RESULTS = ROOT / 'results/work9-recall99-20261015'
TARGETS = [.60,.65,.70,.75,.80,.85,.90,.95,.97,.98,.99]
GRIDS = {
    'ANNProof': list(range(10,41,2))+[48,64,80,100,128,160,200,256,384,512,768,1024],
    'VLAH': list(range(10,51)),
    'Ours': list(range(18,41,2))+[48,64,80,96,112,128,160,192,256,384,512,768,1024],
}

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def event(**kw):
    kw['time']=datetime.now().isoformat()
    with (STUDY/'events.jsonl').open('a') as f: f.write(json.dumps(kw)+'\n')
    print(json.dumps(kw),flush=True)

def run(method,name,parameters,repeats):
    out=RESULTS/(method+'-'+name)
    if not (out/'PASS.txt').exists():
        assert not out.exists(), f'Incomplete output: {out}'
        if method=='Ours':
            cmd=[sys.executable,str(ROOT/'scripts/run_ours_benchmark.py'),
                 '--source',str(ROOT/'build/veriann_lut_benchmark.cpp'),
                 '--binary',str(ROOT/'build/veriann_lut_benchmark'),'--E',','.join(map(str,parameters))]
        else:
            folder='ANNProof-24FGCS' if method=='ANNProof' else 'VLAH-25TrustCom'
            cmd=[sys.executable,str(ROOT/folder/'run.py'),'--ef-search',
                 ','.join(map(str,parameters)) if method=='ANNProof' else '100']
            if method=='VLAH': cmd+=['--probes',','.join(map(str,parameters))]
        cmd+=['--dataset',str(ROOT/'dataset/sift1m'),'--base','1000000','--k','10',
              '--queries','100','--query-offset','100','--repeats',str(repeats),
              '--output',str(out),'--skip-build']
        event(action='start',method=method,name=name,parameters=parameters)
        with (STUDY/(method+'-'+name+'.log')).open('w') as log:
            status=subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        if status.returncode: raise RuntimeError((STUDY/(method+'-'+name+'.log')).read_text()[-6000:])
    groups={}
    with (out/'per_query.csv').open() as f:
        for row in csv.DictReader(f):
            assert row['verified']=='1'
            key=int(row['probes' if method=='VLAH' else 'ef_search'])
            groups.setdefault(key,[]).append(row)
    records=[]
    for p,rows in sorted(groups.items()):
        assert len(rows)==100*repeats
        assert {int(r['query']) for r in rows}==set(range(100,200))
        records.append(dict(method=method,parameter=p,ef=100 if method=='VLAH' else p,
                            probes=p if method=='VLAH' else 1,output=str(out),samples=len(rows),
                            recall=statistics.fmean(float(r['recall_at_k']) for r in rows),
                            query_ms=statistics.fmean(float(r['search_ms'])+float(r['vo_generation_ms']) for r in rows),
                            verify_ms=statistics.fmean(float(r['verification_ms']) for r in rows),
                            vo_kb=statistics.fmean(int(r['vo_bytes'])/1000 for r in rows)))
    (STUDY/(method+'-'+name+'.json')).write_text(json.dumps(records,indent=2)+'\n')
    event(action='passed',method=method,name=name,maximum_recall=max(r['recall'] for r in records))
    return records

def main():
    STUDY.mkdir(parents=True,exist_ok=True)
    RESULTS.mkdir(parents=True,exist_ok=True)
    source=ROOT.parent/'work9.xlsx'
    if not (STUDY/'work9_before_recall99.xlsx').exists():
        shutil.copy2(source,STUDY/'work9_before_recall99.xlsx')
    selection={}
    for method in GRIDS:
        pilot=run(method,'pilot',GRIDS[method],1)
        assert max(r['recall'] for r in pilot)>=.99, (method,'extend grid',pilot[-1])
        chosen=[]
        for target in TARGETS:
            candidates=[r for r in pilot if r['recall']+1e-12>=target]
            r=min(candidates,key=lambda r:r['parameter'])
            chosen.append(dict(target=target,parameter=r['parameter'],pilot_recall=r['recall']))
        assert len({r['parameter'] for r in chosen})==len(TARGETS), (method,'need denser grid',chosen)
        selection[method]=chosen
        (STUDY/'selection.json').write_text(json.dumps(selection,indent=2)+'\n')
    for method,chosen in selection.items():
        records=run(method,'evaluation',[r['parameter'] for r in chosen],3)
        for point in chosen:
            r=next(r for r in records if r['parameter']==point['parameter'])
            assert abs(r['recall']-point['pilot_recall'])<1e-12
            assert r['recall']+1e-12>=point['target']
    event(action='all_measurements_passed')

if __name__=='__main__': main()
