"""Audit old forecasts and collect bounded, explicitly labelled model anchors."""
import os
for k in ['RAYON_NUM_THREADS','OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS']:os.environ[k]='1'
import csv,json,math,shutil,sys,time,resource,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
STUDY=ROOT/'experiments/work9-v3db-reaudit-20261018'
OLD=ROOT/'experiments/work9-v3db-estimates-20261017'
STUDY.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(Path(__file__).parent))
import work9_forecast as previous
previous.STUDY=STUDY
for name in ['gate-observations.json']:
    if not (STUDY/name).exists():shutil.copy2(OLD/name,STUDY/name)
if not (STUDY/'work9_before_reaudit.xlsx').exists():shutil.copy2(ROOT.parent/'work9.xlsx',STUDY/'work9_before_reaudit.xlsx')
refs=previous.references();model=previous.Model(refs)
inventory=[]
for f in previous.CACHE.glob('*/calibration.json'):
    obj=json.loads(f.read_text());c=obj['model']['config']
    if c['dataset'] not in previous.DATA:continue
    for t in [.6,.7,.8,.9,.95,.97,.98,.99]:
        good=[p for p in obj['points'] if p['k']==10 and p['recall']>=t-1e-12]
        for p in good:
            g=max(1,int(previous.features(c,p['n_probe'])/model.scale@model.coef));b=math.ceil(math.log2(g))
            inventory.append(dict(model=f.parent.name,config=c,target=t,recall=p['recall'],probe=p['n_probe'],gate_estimate=g,domain_bit=b,cost=(model.aprove+model.abuild)*2**b*b+model.ahost*model.work(c,p['n_probe'])))
(STUDY/'candidate-configurations.json').write_text(json.dumps(inventory,indent=2))
for ds,(_,n,*_) in previous.DATA.items():
    for t in [.9,.95,.99]:
        rows=[r for r in inventory if r['config']['dataset']==ds and r['config']['N']==n and r['target']==t and r['config']['source']==ds]
        if rows:
            r=min(rows,key=lambda r:r['cost']);print(ds,t,r['model'],r['probe'],r['recall'],r['domain_bit'],round(r['cost']),flush=True)
if len(sys.argv)>1 and sys.argv[1] in ['anchors','anchor19']:
    os.environ['V3DB_RESULTS_DIR']=str(ROOT/'results/work9-v3db-reaudit-20261018')
    from work9_measure import run
    resource.setrlimit(resource.RLIMIT_AS,(50*2**30,50*2**30))
    specifications=[('deep1m-1000000-M96-K1024',2)] if sys.argv[1]=='anchor19' else [('sift1m-1000000',8),('nytimes256-289761-M64',1),('deep1m-1000000-M48',2)]
    for folder,p in specifications:
        print('ANCHOR_START',folder,p,flush=True)
        r=run(previous.CACHE/folder,p,10,1,1,max_gates=500000,force=False)
        print('ANCHOR_DONE',r,flush=True)
