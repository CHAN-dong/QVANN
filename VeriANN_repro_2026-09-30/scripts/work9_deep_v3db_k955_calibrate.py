"""Search-only integer-PQ recall calibration for independently matched Recall@k."""
import os
for key in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','RAYON_NUM_THREADS']:os.environ[key]='1'
import csv,hashlib,json,shutil,sys
from pathlib import Path
import numpy as np
R=Path(__file__).resolve().parents[1];S=R/'experiments/work9-deep-v3db-k955-20261022';S.mkdir(parents=True,exist_ok=True)
SOURCE=R.parent/'work9.xlsx';BACKUP=S/'work9_before_deep_v3db_k955.xlsx'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
if not BACKUP.exists():shutil.copy2(SOURCE,BACKUP)
if (S/'calibration.json').exists():raise SystemExit('Calibration already complete')
sys.path.insert(0,str(R/'V3DB-26VLDB/scripts'))
import work9_calibrate as c
c.STUDY=S
folder,base,queries,meta=c.prepare('deep1m',pq_m=96,pq_k=1024)
centers=np.load(folder/'centers.npy');books=np.rint(np.load(folder/'books.npy')).astype(np.int64)
codes=np.load(folder/'codes.npy',mmap_mode='r');labels=np.load(folder/'labels.npy',mmap_mode='r')
qs=np.stack([c.rescale_query(q,65536,meta['vmin'],meta['vmax']) for q in queries])
print('Computing 20-query integer-PQ distances, one CPU thread',flush=True)
scores=c.score_all_queries(qs,centers,books,codes,labels)
ks=[1,5,10,20,50,100];rows=[];answers=[];ids=np.arange(len(base));truth=C=R/'V3DB-26VLDB/work9-cache/deep1m-1000000/exact-ground-truth'
for qi,q in enumerate(qs):
 gt=np.load(truth/f'query-{100+qi}.npy')[:100]
 order=np.argsort(np.sum((centers-q)**2,axis=1),kind='stable');ranks=np.empty(len(centers),dtype=np.int64);ranks[order]=np.arange(len(centers))
 pq_order=np.lexsort((ids,scores[qi]));coarse_rank=ranks[labels[pq_order]]
 for probe in range(1,65):
  returned=pq_order[coarse_rank<probe][:100]
  assert len(returned)==100
  answers.append(dict(query=qi+100,n_probe=probe,returned=returned.tolist()))
  for k in ks:
   hits=len(set(map(int,returned[:k]))&set(map(int,gt[:k])))
   rows.append(dict(query=qi+100,n_probe=probe,k=k,hits=hits,recall=hits/k))
 print('CALIBRATED query',qi+100,flush=True)
with (S/'per-query-recall.csv').open('w',newline='') as f:
 wr=csv.DictWriter(f,fieldnames=['query','n_probe','k','hits','recall']);wr.writeheader();wr.writerows(rows)
(S/'returned-ids.json').write_text(json.dumps(answers),encoding='utf-8')
points=[];selected=[]
for k in ks:
 for probe in range(1,65):
  rr=[r for r in rows if r['k']==k and r['n_probe']==probe]
  points.append(dict(k=k,n_probe=probe,hits=sum(r['hits'] for r in rr),queries=20,recall=sum(r['hits'] for r in rr)/(20*k)))
 good=[p for p in points if p['k']==k and p['recall']>=.955-1e-12]
 assert good,k
 selected.append(min(good,key=lambda p:p['n_probe']))
record=dict(config=meta['config'],target=.955,selection='independently smallest integer n_probe with Recall@k >=95.5%, grid1..64; exact threshold may be unattainable at this finite sample resolution',points=points,selected=selected,query_ids=list(range(100,120)),role='actual search calibration only; no query/proof/verification timing',source_sha256={str(p):sha(p) for p in [folder/'model.json',folder/'centers.npy',folder/'books.npy',folder/'codes.npy',folder/'labels.npy',Path(c.__file__)]},raw_csv_sha256=sha(S/'per-query-recall.csv'))
(S/'calibration.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
print('SELECTED',json.dumps(selected),flush=True)
