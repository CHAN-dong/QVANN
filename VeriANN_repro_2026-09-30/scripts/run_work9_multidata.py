"""Sequential, resumable full-protocol experiments on the three new datasets."""
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import numpy as np
from work9_multidata import *

def environment():
    env=os.environ.copy();env['OMP_NUM_THREADS']=env['OPENBLAS_NUM_THREADS']='1'
    faiss=ROOT/'third_party/LearnedEarlyTer-20SIGMOD'
    env['LD_LIBRARY_PATH']=':'.join(map(str,[ROOT/'third_party/runtime/lib',faiss,faiss/'lib']))
    return env

def width(dataset):
    file=STUDY/(dataset+'-lsh-width.json')
    if file.exists():return json.loads(file.read_text())['width']
    _,n,d=DATASETS[dataset];path=ROOT/'dataset'/dataset/'base.fvecs'
    a=np.memmap(path,dtype='<f4',mode='r',shape=(n,d+1))
    rng=np.random.default_rng(20261015);ids=rng.integers(0,n,size=(4096,2))
    delta=a[ids[:,0],1:].astype(np.float64)-a[ids[:,1],1:].astype(np.float64)
    value=float(np.median(np.sqrt(np.sum(delta*delta,axis=1)))/4)
    assert math.isfinite(value) and value>0
    file.write_text(json.dumps(dict(width=value,pairs=4096,seed=20261015,rule='median base-only pairwise L2 distance / 4',base_file_sha256=sha(path)),indent=2)+'\n')
    return value

def launch(command,out,log):
    if out.exists() and any(out.iterdir()):raise RuntimeError('Incomplete run needs inspection: '+str(out))
    event(action='start',output=str(out),command=command)
    with log.open('w') as f:status=subprocess.run(command,cwd=ROOT,env=environment(),stdout=f,stderr=subprocess.STDOUT)
    if status.returncode:raise RuntimeError(log.read_text()[-7000:])

def pilot(dataset,method,n,efs,probes,tag,w):
    out=RESULTS/dataset/(method+'-pilot-'+tag)
    if not (out/'PILOT_PASS.json').exists():
        out.parent.mkdir(parents=True,exist_ok=True)
        binary=ROOT/'build'/('multidata_pilot_'+method)
        cache=ROOT/('VLAH-25TrustCom' if method=='VLAH' else 'ANNProof-24FGCS')/'cache'
        cmd=[str(binary),'--dataset',str(ROOT/'dataset'/dataset),'--base',str(n),
             '--output',str(out),'--cache',str(cache),'--queries',str(QUERY_COUNT),'--query-offset',str(QUERY_OFFSET),
             '--build-threads','16','--M',str(INDEX_M),'--ef-construction',str(INDEX_EFC),'--k','10','--ef-search',','.join(map(str,efs)),
             '--probes',','.join(map(str,probes)),'--lsh-width',str(w)]
        launch(cmd,out,STUDY/(dataset+'-'+method+'-pilot-'+tag+'.log'))
        if dataset=='gist1m' and method=='ANNProof' and n==1000000:
            check=STUDY/f'gist-M{INDEX_M}-efc{INDEX_EFC}-reachability.txt'
            if not check.exists():
                index=ROOT/'ANNProof-24FGCS/cache'/f'1d9dfa933a86b1b7_M{INDEX_M}_efc{INDEX_EFC}.index'
                with check.open('w') as f:
                    status=subprocess.run([str(ROOT/'build/inspect_cached_graph'),str(index),'960','1000000'],env=environment(),stdout=f,stderr=subprocess.STDOUT)
                assert status.returncode==0,check.read_text()
                event(action='graph_reachability_checked',details=check.read_text().strip())
        rows=list(csv.DictReader((out/'pilot.csv').open()))
        assert len(rows)==QUERY_COUNT*len(efs)*len(probes)
        meta=dict(command=cmd,binary_sha256=sha(binary),source_sha256=sha(ROOT/'src/experiments/multidata_search_calibration.cpp'),rows=len(rows),role='search-only operating-point selection; NOT timing/VO measurements')
        (out/'PILOT_PASS.json').write_text(json.dumps(meta,indent=2)+'\n')
        event(action='pilot_passed',dataset=dataset,method=method,base=n,rows=len(rows))
    meta=json.loads((out/'PILOT_PASS.json').read_text())
    assert meta['binary_sha256']==sha(ROOT/'build'/('multidata_pilot_'+method))
    assert meta['source_sha256']==sha(ROOT/'src/experiments/multidata_search_calibration.cpp')
    if n>=100000:
        cmd=meta['command']
        assert int(cmd[cmd.index('--M')+1])==INDEX_M
        assert int(cmd[cmd.index('--ef-construction')+1])==INDEX_EFC
    return out

def pilot_groups(out):
    groups={}
    for row in csv.DictReader((out/'pilot.csv').open()):
        if QUERY_OFFSET<=int(row['query'])<QUERY_OFFSET+QUERY_COUNT:groups.setdefault((int(row['ef_search']),int(row['probes'])),[]).append(row)
    assert all(len(rows)==QUERY_COUNT for rows in groups.values())
    return [dict(ef=k[0],probes=k[1],recall=statistics.fmean(float(r['recall_at_k']) for r in rows),valid=all(r['valid']=='1' for r in rows),pilot=str(out)) for k,rows in groups.items()]

def calibrate(dataset,method,n,goal,w):
    points=[]
    if method=='VLAH':
        probes=list(range(1,math.ceil(n/20000)+1))
        for ef in [100,200,400,800,1600,3200,6400]:
            out=pilot(dataset,method,n,[ef],probes,f'{n}-ef{ef}',w)
            points=pilot_groups(out)
            if max(p['recall'] for p in points)>=goal-1e-12:break
    else:
        grid=([8,12,16,20,24,32,48,64,96,128,192,256,384,512,768,1024,1536,2048] if method=='Ours' else [10,12,16,20,24,32,48,64,96,128,192,256,384,512,768,1024,1536,2048])
        for phase,extra in enumerate([grid,[4096,8192],[16384,32768],[65536,131072],[262144,524288]]):
            extra=[v for v in extra if v<=n]
            if not extra:continue
            out=pilot(dataset,method,n,extra,[1],f'{n}-phase{phase}',w);points+=pilot_groups(out)
            if max(p['recall'] for p in points if p['valid'])>=goal-1e-12:break
    assert max(p['recall'] for p in points if p['valid'])>=goal-1e-12,(dataset,method,n,'recall target not reached')
    # Retain completed full-protocol curve points; recompute recall on the chosen 20 IDs.
    completed=RESULTS/dataset/(method+'-curve-'+str(n))
    if goal==.99 and (completed/'PASS.txt').exists():
        available={(r['ef'],r['probes']) for r in measurements(completed)}
        points=[p for p in points if (p['ef'],p['probes']) in available]
    targets=TARGETS if goal==.99 else [.95]
    selected=[]
    for target in targets:
        eligible=[p for p in points if p['valid'] and p['recall']>=target-1e-12]
        chosen=min(eligible,key=lambda p:(p['probes'] if method=='VLAH' else p['ef']))
        selected.append(dict(chosen,target=target))
    file=STUDY/(dataset+'-'+method+f'-selection-{n}-{goal}.json')
    file.write_text(json.dumps(selected,indent=2)+'\n')
    event(action='selected',dataset=dataset,method=method,base=n,start=selected[0]['recall'],end=selected[-1]['recall'],ef=selected[-1]['ef'],probes=selected[-1]['probes'])
    return selected

def compare_pilot(output,selection):
    rows=[r for r in csv.DictReader((output/'per_query.csv').open()) if QUERY_OFFSET<=int(r['query'])<QUERY_OFFSET+QUERY_COUNT]
    total=0
    for point in selection:
        prior=list(csv.DictReader((Path(point['pilot'])/'pilot.csv').open()))
        lookup={int(r['query']):r for r in prior if (int(r['ef_search']),int(r['probes']))==(point['ef'],point['probes'])}
        for row in rows:
            if int(row['k'])!=10 or (int(row['ef_search']),int(row['probes']))!=(point['ef'],point['probes']):continue
            old=lookup[int(row['query'])]
            assert abs(float(row['recall_at_k'])-float(old['recall_at_k']))<1e-10
            for k in ['visited','expanded']:assert row[k]==old[k],(output,k,row['query'])
            total+=1
    assert total>=QUERY_COUNT*REPEATS
    (output/'pilot_equivalence.json').write_text(json.dumps(dict(status='PASS',comparisons=total,columns=['recall_at_k','visited','expanded']),indent=2)+'\n')

def evaluate(dataset,method,n,purpose,selection,w):
    tag=f'{dataset}-{method}-{purpose}-{n}';record_file=STUDY/('evaluation-'+tag+'.json')
    out=RESULTS/dataset/(method+'-'+purpose+'-'+str(n))
    efs=sorted({p['ef'] for p in selection});probes=sorted({p['probes'] for p in selection})
    ks=[1,5,10,20,50,100] if purpose=='k' else [10]
    if method!='Ours' and purpose=='k':efs=[max(100,efs[0])]
    if (out/'PASS.txt').exists():
        wanted={(k,e,p) for k in ks for e in efs for p in probes}
        available={(r['k'],r['ef'],r['probes']) for r in measurements(out)}
        if not wanted<=available:
            archive=out.parent/'retained-100query'/out.name
            archive.parent.mkdir(exist_ok=True)
            assert out.resolve().is_relative_to(ROOT.resolve()) and archive.resolve().is_relative_to(ROOT.resolve())
            out.rename(archive)
            if record_file.exists():record_file.rename(archive/'previous_evaluation.json')
    if not (out/'PASS.txt').exists():
        if method=='Ours':
            cmd=[sys.executable,str(ROOT/'scripts/run_ours_benchmark.py'),'--source',str(ROOT/'build/veriann_lut_benchmark.cpp'),
                 '--binary',str(ROOT/'build/veriann_lut_benchmark'),'--E',','.join(map(str,efs))]
        else:
            folder='ANNProof-24FGCS' if method=='ANNProof' else 'VLAH-25TrustCom'
            cmd=[sys.executable,str(ROOT/folder/'run.py'),'--ef-search',','.join(map(str,efs)),
                 '--probes',','.join(map(str,probes)),'--lsh-width',str(w)]
        cmd+=['--dataset',str(ROOT/'dataset'/dataset),'--base',str(n),'--queries',str(QUERY_COUNT),'--query-offset',str(QUERY_OFFSET),
              '--repeats','3','--build-threads','16','--M',str(INDEX_M),'--ef-construction',str(INDEX_EFC),'--k',','.join(map(str,ks)),'--output',str(out),'--skip-build']
        launch(cmd,out,STUDY/(tag+'.log'))
    rows=measurements(out);assert sum(r['samples'] for r in rows)==QUERY_COUNT*REPEATS*len(ks)*len(efs)*len(probes)
    if purpose!='k' or efs==[selection[0]['ef']]:compare_pilot(out,selection)
    record=dict(dataset=dataset,method=method,base=n,purpose=purpose,output=str(out),selection=selection,width=w,M=INDEX_M,ef_construction=INDEX_EFC,query_count=QUERY_COUNT,query_offset=QUERY_OFFSET,repeats=REPEATS,measurement_scope="20-query quick preview")
    record_file.write_text(json.dumps(record,indent=2)+'\n');checkpoint()
    event(action='evaluation_passed',dataset=dataset,method=method,purpose=purpose,base=n)
    return record

def main():
    initialize()
    validation=json.loads((ROOT/'dataset/extra_datasets_final_validation.json').read_text());assert validation['status']=='PASS'
    # Each protocol runs sequentially; only graph construction uses multiple cores.
    for dataset,(name,n,d) in DATASETS.items():
        w=width(dataset)
        for method in METHODS:
            selected=calibrate(dataset,method,n,.99,w)
            curve=evaluate(dataset,method,n,'curve',selected,w)
            evaluate(dataset,method,n,'k',[selected[7]],w)
            for size in [100000,500000,1000000]:
                if size==n:
                    alias=dict(curve,purpose='size',filter=dict(k=10,ef=selected[7]['ef'],probes=selected[7]['probes']))
                    file=STUDY/(f'evaluation-{dataset}-{method}-size-{size}.json')
                    file.write_text(json.dumps(alias,indent=2)+'\n');checkpoint()
                else:
                    p=calibrate(dataset,method,size,.95,w)
                    evaluate(dataset,method,size,'size',p,w)
        event(action='dataset_complete',dataset=dataset)
    checkpoint();event(action='all_datasets_complete')

if __name__=='__main__':main()
