"""Reproducible experiment matrix for work9.xlsx. Sequential timing, resumable."""
import argparse
import csv
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
STUDY = ROOT / 'experiments/work9-20261015'
STUDY.mkdir(parents=True, exist_ok=True)
EVENTS = STUDY / 'events.jsonl'

def event(**fields):
    fields['time'] = datetime.now().isoformat()
    with EVENTS.open('a') as f:
        f.write(json.dumps(fields)+'\n')
    print(json.dumps(fields), flush=True)

def run(method, name, dataset, n=1000000, queries=100, offset=100,
        repeats=3, k='10', ef='100', probes='41', build=False, build_threads=1):
    folder = ROOT / method
    out = folder / 'results' / 'work9-20261015' / name
    recorded=STUDY/(method+'-'+name+'.json')
    if not (out/'PASS.txt').exists() and recorded.exists():
        prior=json.loads(recorded.read_text())
        candidate=Path(prior['output'])
        if (candidate/'PASS.txt').exists():
            out=candidate
    # Only resume a run from this study when all its verification checks passed.
    if (out/'PASS.txt').exists():
        event(action='resume_passed',method=method,experiment=name)
    else:
        if out.exists() and any(out.iterdir()):
            out = out.with_name(name+'-retry-'+datetime.now().strftime('%H%M%S'))
        command = [sys.executable,str(folder/'run.py'),'--dataset',str(dataset),
                   '--base',str(n),'--queries',str(queries),'--query-offset',str(offset),
                   '--build-threads',str(build_threads),
                   '--repeats',str(repeats),'--k',str(k),'--ef-search',str(ef),
                   '--probes',str(probes),'--output',str(out)]
        if not build:
            command.append('--skip-build')
        event(action='start',method=method,experiment=name,command=command)
        with (STUDY/(method+'-'+name+'.console.log')).open('w') as log:
            result = subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        if result.returncode:
            event(action='failed',method=method,experiment=name,exit=result.returncode)
            raise RuntimeError((STUDY/(method+'-'+name+'.console.log')).read_text()[-8000:])
        event(action='passed',method=method,experiment=name,output=str(out))
    with (out/'summary.csv').open() as f:
        rows = list(csv.DictReader(f))
    record = {'method':method,'name':name,'dataset':str(dataset),'base':n,
              'queries':queries,'query_offset':offset,'repeats':repeats,
              'output':str(out),'summary':rows}
    (STUDY/(method+'-'+name+'.json')).write_text(json.dumps(record,indent=2)+'\n')
    subprocess.run([sys.executable,str(ROOT/'scripts/fill_work9.py')],cwd=ROOT,check=True)
    return record

def sift_phase():
    dataset=ROOT/'dataset/sift1m'
    run('ANNProof-24FGCS','sift1m-recall-curve',dataset,
        ef='10,15,20,30,40,50,75,100,150,200,300',build=True)
    run('VLAH-25TrustCom','sift1m-recall-curve',dataset,ef='50',
        probes='1,3,5,9,15,21,29,35,41,45,50',build=True)
    run('ANNProof-24FGCS','sift1m-k-curve',dataset,k='1,5,10,20,50,100',ef='100',build=True)
    run('VLAH-25TrustCom','sift1m-k-curve',dataset,k='1,5,10,20,50,100',ef='100',probes='41',build=True)

def choose(record, field):
    rows=record['summary']
    eligible=[r for r in rows if float(r['recall_at_k'])>=.95]
    selected=min(eligible,key=lambda r:int(r[field])) if eligible else max(rows,key=lambda r:float(r['recall_at_k']))
    return int(selected[field]),float(selected['recall_at_k'])

def size_phase(dataset, maximum, repeats):
    for n in [100000,500000,1000000,5000000,10000000]:
        if n>maximum:
            continue
        # Calibration uses queries 0..19. Evaluation uses disjoint 100..199.
        a=run('ANNProof-24FGCS',f'size-{n}-calibration',dataset,n=n,queries=20,
              offset=0,repeats=1,ef='20,50,100,200,400,800',build=n==100000,build_threads=8)
        ef,recall=choose(a,'ef_search')
        run('ANNProof-24FGCS',f'size-{n}-evaluation',dataset,n=n,repeats=repeats,ef=str(ef),build_threads=8)
        buckets=math.ceil(n/20000)
        probe_list=sorted(set(max(1,min(buckets,math.ceil(buckets*x))) for x in [.05,.1,.2,.3,.4,.5,.6,.7,.8,.9,1]))
        b=run('VLAH-25TrustCom',f'size-{n}-calibration',dataset,n=n,queries=20,
              offset=0,repeats=1,ef='100',probes=','.join(map(str,probe_list)),build=n==100000,build_threads=8)
        probes,recall=choose(b,'probes')
        run('VLAH-25TrustCom',f'size-{n}-evaluation',dataset,n=n,repeats=repeats,ef='100',probes=str(probes),build_threads=8)

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--phase',choices=['sift','size'],required=True)
    parser.add_argument('--dataset',type=Path,default=ROOT/'dataset/bigann10m')
    parser.add_argument('--max-base',type=int,default=10000000)
    parser.add_argument('--repeats',type=int,default=3)
    args=parser.parse_args()
    if args.phase=='sift':
        sift_phase()
    else:
        size_phase(args.dataset.resolve(),args.max_base,args.repeats)
