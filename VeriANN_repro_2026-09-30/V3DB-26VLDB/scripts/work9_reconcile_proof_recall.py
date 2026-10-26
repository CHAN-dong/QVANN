"""Reconcile one completed proof point with the prover's stable tie order.

Preserves the original search calibration, all proof timings and original raw CSVs.
"""
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
REPRO=ROOT/'V3DB-26VLDB'
sys.path.insert(0,str(REPRO/'upstream'))
from ivf_pq import rescale_query
from zk_IVF_PQ.zk_IVF_PQ import py_work9_verify_wire

name='nytimes256-289761-M256'
folder=REPRO/'work9-cache'/name
out=ROOT/'results/work9-v3db-20261016'/f'{name}-p2-k10-q20-r3-t1'
file=folder/'calibration.json'
cal=json.loads(file.read_text())
if any(x.get('job')==out.name for x in cal.get('proof_reconciliations',[])):
    print('Already reconciled');sys.exit(0)
result=json.loads((out/'result.json').read_text());assert result['status']=='passed'
rows=list(csv.DictReader((out/'per_query.csv').open()))
assert len(rows)==60 and all(r['verified']=='1' for r in rows)
assert {(int(r['query']),int(r['repeat'])) for r in rows}=={(q,t) for q in range(100,120) for t in range(3)}
backup=folder/'calibration.before-wire-reconciliation.json'
assert not backup.exists()
shutil.copy2(file,backup)
meta=json.loads((folder/'model.json').read_text());c=meta['config']
centers=np.load(folder/'centers.npy').astype(np.int64)
books=np.rint(np.load(folder/'books.npy')).astype(np.int64)
codes=np.load(folder/'codes.npy',mmap_mode='r')
g=np.load(folder/'groups.npz');groups={int(k):g[k] for k in g.files}
queries=np.memmap(ROOT/'dataset/nytimes256/query.fvecs',dtype='<f4',mode='r').reshape(-1,257)[:,1:]
proof=(out/'query0.proof').read_bytes()
public=py_work9_verify_wire(proof,(out/'trusted_verifier.bin').read_bytes())
start=len(public)-(10+2+c['n_list']+2*c['capacity']+c['D'])
changed_order=0;changed_metric=0
for qid in range(100,120):
    q=rescale_query(queries[qid],65536,meta['vmin'],meta['vmax'])
    order=np.argsort(np.sum((centers-q)**2,axis=1),kind='stable')[:2]
    ids=[];scores=[]
    for cluster in order:
        cluster_ids=groups[int(cluster)]
        reconstructed=books[np.arange(c['M'])[None,:],codes[cluster_ids]].reshape(len(cluster_ids),c['D'])
        delta=q-centers[cluster]-reconstructed
        ids.extend(cluster_ids.tolist());scores.extend(np.sum(delta*delta,axis=1).tolist())
    returned=np.asarray(ids)[np.argsort(np.asarray(scores,dtype=np.int64),kind='stable')[:10]].tolist()
    if qid==100:assert returned==public[start:start+10]
    measured={float(r['recall']) for r in rows if int(r['query'])==qid};assert len(measured)==1
    measured=measured.pop()
    entry=next(x for x in cal['per_query'] if x['query']==qid and x['k']==10 and x['n_probe']==2)
    changed_order+=entry['returned']!=returned
    changed_metric+=abs(entry['recall']-measured)>1e-12
    entry['before_proof_reconciliation']=dict(returned=entry['returned'],recall=entry['recall'])
    entry['returned']=returned;entry['recall']=measured
point=next(p for p in cal['points'] if p['n_probe']==2 and p['k']==10)
previous=point['recall'];point['before_proof_reconciliation_recall']=previous;point['recall']=result['recall']
note=dict(job=out.name,n_probe=2,k=10,original_calibration_recall=previous,formal_measured_recall=result['recall'],
    queries_with_changed_returned_order=changed_order,queries_with_changed_recall=changed_metric,
    source=str(out/'per_query.csv'),source_sha256=hashlib.sha256((out/'per_query.csv').read_bytes()).hexdigest(),
    original_calibration=str(backup),
    explanation='Calibration sorted PQ score ties by global ID and obtained nested GT via top100 partition. Actual prover uses stable cluster/slot order and formal metric partitions ground truth at k. Updated this completed point to the actual formal metric; other points remain search-only calibration estimates.')
cal.setdefault('proof_reconciliations',[]).append(note)
cal['role']='search calibration; listed proof_reconciliations override completed points with actual measured semantics; original file retained'
tmp=file.with_suffix('.json.tmp');tmp.write_text(json.dumps(cal,indent=2)+'\n');tmp.replace(file)
(ROOT/'experiments/work9-v3db-20261016/nytimes-recall-reconciliation.json').write_text(json.dumps(note,indent=2)+'\n')
print(json.dumps(note,indent=2))
