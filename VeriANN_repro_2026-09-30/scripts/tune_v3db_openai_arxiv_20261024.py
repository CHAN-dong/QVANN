"""Increase ArXiv PQ fidelity within the declared real proof budget; no forecasts.

Run after the main native trials so training cannot contaminate their timings.
Retain every M128 measurement; add at most one fixed-query proof for a finer PQ.
"""
import argparse,csv,json,math,os,sys,time
from pathlib import Path
import work9_resource_matched as u
R=u.ROOT;S=R/'experiments/openai-arxiv-20261024';O=R/'results/openai-arxiv-20261024';KEY='openai_arxiv1m';N=1000000;D=1536
u.STUDY,u.RESULTS=S,O
def worker(K):
    sys.path.insert(0,str(R/'V3DB-26VLDB/scripts'));import work9_calibrate as c
    c.DATA[KEY]=('OpenAI-ArXiv-1M',N,D,1024,8);c.STUDY=S/'V3DB-tuning';c.STUDY.mkdir(exist_ok=True)
    c.calibrate(KEY,pq_m=256,pq_k=K)
def readpoint(r,model):
    if r.get('status')!='measured':return None
    rr=list(csv.DictReader(Path(r['source']).open()));assert len(rr)==1 and rr[0]['verified']=='1'
    meta=u.read(R/'V3DB-26VLDB/work9-cache'/model/'model.json')
    return dict(r,model=model,config=meta['config'],gates=int(rr[0]['gates']),query_sample=100,query_repeats=1)
def run(wait):
    while wait and not (S/'FINISHED.json').exists():time.sleep(5)
    if (S/'V3DB-tuning-finished.json').exists():return
    assert (S/'FINISHED.json').exists(),'Finish all main timings before PQ tuning'
    prior=S/'before-V3DB-PQ-tuning';prior.mkdir(exist_ok=True)
    for name in ['V3DB-overall.json','V3DB-curve.json','V3DB-index.json']:
        p=S/'records'/name
        if p.exists() and not (prior/name).exists():(prior/name).write_bytes(p.read_bytes())
    old_model=f'{KEY}-{N}-M128';old=u.read(prior/'V3DB-overall.json');points=[]
    point=readpoint(old,old_model)
    if point:points.append(point)
    # Prespecified extra family: double PQ subspaces, select largest K among
    #1024,512,256 whose TRUE gate count stays within the same2^20-domain budget.
    # The decision uses only circuit shape, before training/accuracy/timing.
    gates=[];chosen=None
    for K in [1024,512,256]:
        cfg=dict(dataset=KEY,source=KEY,N=N,D=D,n_list=1024,M=256,K=K,capacity=2048)
        gate=u.static_gate(cfg,1,10);gates.append(dict(config=cfg,gate=gate))
        if gate['status']=='passed':chosen=K;break
    u.write(S/'V3DB-PQ-tuning-gates.json',gates)
    if chosen is None:
        u.write(S/'V3DB-tuning-finished.json',dict(status='no_finer_tested_shape_within_budget',baseline_preserved=True));return
    model=f'{KEY}-{N}-M256'+(f'-K{chosen}' if chosen!=256 else '')
    folder=R/'V3DB-26VLDB/work9-cache'/model
    if not (folder/'calibration.json').exists():
        u.command([sys.executable,Path(__file__).resolve(),'worker','--K',chosen],S/'logs'/f'V3DB-PQ-tune-M256-K{chosen}.log',bounded=True,timeout=7200)
    calibration=u.read(folder/'calibration.json');p=next(x for x in calibration['points'] if x['k']==10 and x['n_probe']==1)
    out=O/'V3DB'/f'{model}-p1-k10-q1-r1-t1';env=u.environment();env['V3DB_RESULTS_DIR']=str(O/'V3DB')
    if not (out/'result.json').exists():
        import subprocess
        cmd=[sys.executable,R/'V3DB-26VLDB/scripts/work9_measure.py','--model',model,'--probe',1,'--k',10,'--queries',1,'--repeats',1,'--attempt-large']
        with (S/'logs'/f'V3DB-PQ-tune-M256-K{chosen}-proof.log').open('w') as f:
            result=subprocess.run(list(map(str,cmd)),cwd=R,env=env,stdout=f,stderr=subprocess.STDOUT,preexec_fn=u.limit)
        if result.returncode:
            u.write(S/'V3DB-tuning-finished.json',dict(status='finer_PQ_proof_failed',model=model,code=result.returncode,baseline_preserved=True,calibration=str(folder/'calibration.json')));return
    native=u.read(out/'result.json');assert native['status']=='passed' and native['samples']==1
    rr=list(csv.DictReader((out/'per_query.csv').open()));assert len(rr)==1 and rr[0]['verified']=='1';r=rr[0]
    tamper=u.read(out/'tamper.json');assert all(tamper.values())
    new=dict(method='V3DB',status='measured',model=model,config=calibration['model']['config'],ef=1,probes=1,recall=p['recall'],query_ms=float(r['query_ms']),vo_kb=float(r['vo_bytes'])/1000,verify_ms=float(r['verify_ms']),gates=int(r['gates']),samples=1,query_sample=100,query_repeats=1,source=str(out/'per_query.csv'),source_sha256=u.sha(out/'per_query.csv'),recall_scope='20 search-only queries, one fixed query100 real proof/timing; not an average of20 proofs')
    points.append(new)
    # Report actual accuracy of every measured configuration, including the
    # weaker M128 model, rather than hiding the unsuccessful first calibration.
    from run_openai_arxiv_20261024 import TARGETS,save
    curve=[]
    for target in TARGETS:
        good=[x for x in points if x['recall']>=target-1e-12]
        chosen_point=min(good,key=lambda x:(x['gates'],x['config']['M'],x['config']['K'])) if good else None
        curve.append(dict(chosen_point,target=target) if chosen_point else dict(status='unmeasured_or_resource_limit',target=target,reason='No actual verified proof at this recall. Search-only full-probe recalls and real gate limits are recorded separately.'))
    preferred=max(points,key=lambda x:(x['recall'],-x['gates']))
    save('V3DB-curve',dict(method='V3DB',kind='curve',points=curve,configuration_points=points,parameter_policy='M128 original plus finer M256 PQ, largest shape-feasible K. At most one fixed-query proof per configuration. Target mappings select smallest true gate count; actual raw timings untouched.'))
    save('V3DB-overall',dict(preferred,kind='overall'))
    # A previously exported index belongs to the previous model and cannot be
    # silently paired with the new default. Preserve it before re-exporting.
    index=S/'records/V3DB-index.json'
    if index.exists() and u.read(index).get('model')!=preferred['model']:
        index.rename(prior/('previous-index-'+str(time.time_ns())+'.json'))
        serving=O/'indexes/V3DB'
        if serving.exists():
            assert serving.resolve().parent==(O/'indexes').resolve()
            serving.rename(serving.with_name('V3DB-prior-'+str(time.time_ns())))
    columns=['model','recall','query_ms','vo_kb','verify_ms','gates','samples','source','source_sha256']
    with (S/'V3DB-actual-PQ-points.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=columns);w.writeheader();w.writerows({k:x.get(k,'') for k in columns} for x in points)
    u.write(S/'V3DB-tuning-finished.json',dict(status='completed_actual_proofs',model=model,preferred_model=preferred['model'],all_verified_configurations=points,original_M128_full_probe_recall=max(x['recall'] for x in u.read(R/'V3DB-26VLDB/work9-cache'/old_model/'calibration.json')['points'] if x['k']==10),new_full_probe_recall=max(x['recall'] for x in calibration['points'] if x['k']==10),declared_gate_budget=2**20,search_grid_probes=[x['n_probe'] for x in calibration['points'] if x['k']==10]))
    print('V3DB_PQ_TUNING_COMPLETE',preferred['model'],preferred['recall'],flush=True)
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('stage',choices=['worker','run'],default='run',nargs='?');parser.add_argument('--K',type=int,default=256);parser.add_argument('--after-main',action='store_true');args=parser.parse_args()
    if args.stage=='worker':worker(args.K)
    else:run(args.after_main)
