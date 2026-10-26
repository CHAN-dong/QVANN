"""Small static gate counts only: no circuit build FFT, witness or proving."""
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
STUDY=ROOT/'experiments/work9-v3db-estimates-20261017'
os.environ['RAYON_NUM_THREADS']=os.environ['OMP_NUM_THREADS']=os.environ['OPENBLAS_NUM_THREADS']='1'

if len(sys.argv)>1 and sys.argv[1]=='--one':
    resource.setrlimit(resource.RLIMIT_AS,(8*2**30,8*2**30))
    from zk_IVF_PQ.zk_IVF_PQ import py_set_based_gate
    c=json.loads(sys.argv[2])
    g=py_set_based_gate(c['M'],c['K'],c['D']//c['M'],c['n_list'],c['n_probe'],c['capacity'],c['k'],True)
    print(json.dumps(dict(config=c,gates=g,role='exact static gate count only; no proof or timing measurement')))
    sys.exit(0)

STUDY.mkdir(parents=True,exist_ok=True)
target=STUDY/'gate-observations.json'
records=json.loads(target.read_text()) if target.exists() else []
known={json.dumps(x['config'],sort_keys=True) for x in records}
selected=json.loads((ROOT/'experiments/work9-v3db-20261016/selected-models.json').read_text())
dimensions={'sift1m':(128,1024),'nytimes256':(256,512),'glove100':(100,1024),'deep1m':(96,1024)}
configs=[]
for key,(d,l) in dimensions.items():
    for cap in [64,256,1024]:
        for p in [1,2]:configs.append(dict(D=d,M=selected[key]['M'],K=selected[key]['K'],n_list=l,n_probe=p,capacity=cap,k=10))
    for cap in [256]:
        for p in [1,2]:configs.append(dict(D=d,M=selected[key]['M'],K=selected[key]['K'],n_list=256,n_probe=p,capacity=cap,k=10))
for i,c in enumerate(configs):
    if json.dumps(c,sort_keys=True) in known:continue
    done=subprocess.run([sys.executable,str(Path(__file__).resolve()),'--one',json.dumps(c)],capture_output=True,text=True,timeout=60)
    if done.returncode:raise RuntimeError(done.stdout+done.stderr)
    r=json.loads(done.stdout.strip().splitlines()[-1]);records.append(r)
    target.write_text(json.dumps(records,indent=2)+'\n')
    print(json.dumps(dict(progress=f'{i+1}/{len(configs)}',**r)),flush=True)
