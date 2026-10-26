"""Bounded static gate audit; never build FFT/prove at these larger shapes."""
import os
for k in ['RAYON_NUM_THREADS','OMP_NUM_THREADS','OPENBLAS_NUM_THREADS']:os.environ[k]='1'
import json,resource,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
STUDY=ROOT/'experiments/work9-v3db-reaudit-20261018'
large=len(sys.argv)>1 and sys.argv[1]=='--large'
if len(sys.argv)>1 and not large:
    budget=int(os.environ.get('V3DB_GATE_AS_GIB','12'))
    resource.setrlimit(resource.RLIMIT_AS,(budget*2**30,budget*2**30))
    from zk_IVF_PQ.zk_IVF_PQ import py_set_based_gate
    c=json.loads(sys.argv[1]);g=py_set_based_gate(c['M'],c['K'],c['D']//c['M'],c['n_list'],c['n_probe'],c['capacity'],10,True)
    print(json.dumps(dict(config=c,gates=g,role='reaudit: exact static constraints only; no proving FFT')));sys.exit(0)
old=json.loads((ROOT/'experiments/work9-v3db-estimates-20261017/overall-forecast-summary.json').read_text())
shapes=[]
for r in old:
    ds=r['dataset'];c=r['config']
    for p in ([16] if ds in ['sift1m','deep1m'] else ([4] if large else [1,2,4])):shapes.append(dict(c,n_probe=p,k=10))
records=json.loads((STUDY/'gate-observations.json').read_text());audit=[]
for i,c in enumerate(shapes):
    print('STATIC_START',c['dataset'],c['n_probe'],flush=True)
    try:
        env=os.environ.copy();env['V3DB_GATE_AS_GIB']='40' if large else '12'
        r=subprocess.run([sys.executable,__file__,json.dumps(c)],capture_output=True,text=True,timeout=120)
        (STUDY/f'gate-audit-{"large-" if large else ""}{i}.log').write_text(r.stdout+r.stderr)
        if r.returncode:entry=dict(config=c,status='static count exceeded address space budget or failed',budget_GiB=int(env['V3DB_GATE_AS_GIB']),code=r.returncode)
        else:
            entry=json.loads(r.stdout.strip().splitlines()[-1]);entry['status']='counted';records.append({k:entry[k] for k in ['config','gates','role']})
    except subprocess.TimeoutExpired:entry=dict(config=c,status='static count exceeded 120s; not measured')
    audit.append(entry);(STUDY/('gate-reaudit-large.json' if large else 'gate-reaudit.json')).write_text(json.dumps(audit,indent=2));(STUDY/'gate-observations.json').write_text(json.dumps(records,indent=2))
    print('STATIC_DONE',entry,flush=True)
