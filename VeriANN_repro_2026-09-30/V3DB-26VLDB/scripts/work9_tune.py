"""Tune PQ precision on the exact same 20-query workload; retain every trial."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from work9_calibrate import ROOT, REPRO, STUDY, CACHE, DATA, event


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--wait-base',action='store_true')
    p.add_argument('--datasets',nargs='+',choices=list(DATA))
    a=p.parse_args()
    replaced=(ROOT/'experiments/work9-nytimes-20261017/replacement.json').exists()
    active={k:v for k,v in DATA.items() if k in (a.datasets or [k for k in DATA if k!=('gist1m' if replaced else 'nytimes256')])}
    if a.wait_base:
        while not all((CACHE/f'{key}-{v[1]}'/'calibration.json').exists() for key,v in active.items()):
            time.sleep(20)
    selection=json.loads((STUDY/'selected-models.json').read_text()) if (STUDY/'selected-models.json').exists() else {}
    for dataset,(sheet,n,d,lists,base_m) in active.items():
        trials=[]
        # First inspect the original setting, then subvector dimensions 4,2,1.
        grid=sorted({base_m,*[d//sub for sub in [4,2,1] if d%sub==0]})
        for M,K in [(m,256) for m in grid]+[(d,512),(d,1024)]:
            name=f'{dataset}-{n}'+(f'-M{M}' if M!=base_m else '')+(f'-K{K}' if K!=256 else '')
            folder=CACHE/name
            file=folder/'calibration.json'
            if not file.exists():
                log=STUDY/f'tune-{dataset}-M{M}.log'
                env=dict(os.environ,OMP_NUM_THREADS='16',OPENBLAS_NUM_THREADS='1',PYTHONUNBUFFERED='1')
                with log.open('a') as f:
                    status=subprocess.run([sys.executable,REPRO/'scripts/work9_calibrate.py',
                          '--dataset',dataset,'--M',str(M),'--K',str(K)],env=env,stdout=f,stderr=subprocess.STDOUT)
                if status.returncode:raise RuntimeError(f'Tuning failed: {log}')
            cal=json.loads(file.read_text())
            points=[x for x in cal['points'] if x['k']==10]
            best=max(points,key=lambda x:x['recall'])
            trials.append(dict(M=M,K=K,model=name,best_grid_recall10=best['recall'],best_probe=best['n_probe']))
            event(action='tune_trial',dataset=dataset,**trials[-1])
            if best['recall']>=.99-1e-12:break
        eligible=[x for x in trials if x['best_grid_recall10']>=.99-1e-12]
        chosen=min(eligible,key=lambda x:(x['M'],x['K'])) if eligible else max(trials,key=lambda x:x['best_grid_recall10'])
        selection[dataset]=dict(chosen,query_count=20,repeats=3,target=.99,trials=trials,
            policy='smallest tested M reaching 99% somewhere in measured probe grid; otherwise highest measured recall; same-query tuning, not held-out')
        (STUDY/'selected-models.json').write_text(json.dumps(selection,indent=2)+'\n')
    print(json.dumps(selection,indent=2))
    (STUDY/'tuning-complete.json').write_text(json.dumps(dict(version=2,selected=selection),indent=2)+'\n')


if __name__=='__main__':main()
