"""VO disaggregation from real samples; fill missing full-scale GloVe ablations."""
import csv
import json
import math
import shutil
import statistics
from pathlib import Path

import work9_resource_matched as u

R=u.ROOT;S=R/'experiments/work9-vo-breakdown-20261023';O=R/'results/work9-vo-breakdown-20261023'
u.STUDY,u.RESULTS=S,O;S.mkdir(parents=True,exist_ok=True)
backup=S/'work9_before_vo_breakdown.xlsx'
if not backup.exists():shutil.copy2(R.parent/'work9.xlsx',backup)
DATA={'sift1m':('SIFT1M',1000000,128),'nytimes256':('NYTimes-256',289761,256),'glove100':('GloVe-100',1183514,100),'deep1m':('DEEP1M',1000000,96)}
MODES=['MR-tree','ANNProof','VLAH','IVF-ADS','HNSW-ADS']
source=R/'scripts/mr_vo_breakdown_probe.cpp';binary=S/'mr_breakdown'
if not binary.exists():
    cmd=['g++','-O3','-DNDEBUG','-std=c++17',source,'-lcrypto','-o',binary]
    u.command(cmd,S/'build.log',timeout=300)
    u.write(S/'build.json',dict(command=list(map(str,cmd)),source_sha256=u.sha(source),original_mr_sha256=u.sha(R/'Chen13-MR/mr.cpp'),binary_sha256=u.sha(binary)))
for ds,(sheet,n,d) in DATA.items():
    out=O/ds/'MR-tree'
    if not (out/'PASS.txt').exists():
        u.command([binary,R/'dataset'/ds/'sift_base.fvecs',R/'dataset'/ds/'sift_query.fvecs',n,out],S/(ds+'-mr.log'),bounded=True,timeout=3600)

# Existing GloVe ablation used a 1M prefix; never mix it with full-scale baselines.
cache=R/'ANNProof-24FGCS/cache';ablation=R/'build/work9_ablation'
common=[ablation,'--dataset',R/'dataset/glove100','--base',1183514,'--M',16,'--ef-construction',200,'--cache',cache,'--queries',20,'--query-offset',100,'--repeats',3,'--build-threads',1,'--k',10,'--nlist',256]
chosen={}
for variant,grid in [(1,[32,48,64,80,96,128,192,256]),(2,[256,512,768,1024,1280,1536,2048])]:
    pilot=O/'glove100'/f'pilot-{variant}'
    if not (pilot/'PILOT_PASS.txt').exists():
        u.command(common+['--output',pilot,'--variants',variant,'--pilot',1,'--ef-search',','.join(map(str,grid))],S/f'glove100-pilot-{variant}.log',bounded=True,timeout=3600)
    rows=list(csv.DictReader((pilot/'pilot.csv').open()))
    values={p:statistics.fmean(float(r['recall_at_k']) for r in rows if int(r['parameter'])==p) for p in grid}
    p=next((p for p in grid if values[p]>=.95-1e-12),None);assert p is not None,(variant,values)
    chosen[variant]=dict(parameter=p,recall=values[p],calibration=values)
    out=O/'glove100'/f'ablation-{variant}'
    if not (out/'PASS.txt').exists():
        u.command(common+['--output',out,'--variants',variant,'--pilot',0,'--ef-search',p],S/f'glove100-formal-{variant}.log',bounded=True,timeout=3600)
u.write(S/'glove-calibration.json',chosen)

sources={
 'sift1m':{'ANNProof':('ANNProof-24FGCS/results/work9-20261015/sift1m-recall-curve/per_query.csv',100,1),'VLAH':('VLAH-25TrustCom/results/work9-20261015/sift1m-recall-curve/per_query.csv',50,41)},
 'nytimes256':{'ANNProof':('results/work9-nytimes-annproof-full-20261021/measurements/default-and-k/per_query.csv',264,1),'VLAH':('results/work9-nytimes-20261017/nytimes256/VLAH-curve-289761/per_query.csv',None,None)},
 'glove100':{'ANNProof':('results/work9-multidata-20261015/glove100/ANNProof-curve-1183514/per_query.csv',1536,1),'VLAH':('results/work9-multidata-20261015/glove100/VLAH-curve-1183514/per_query.csv',200,56)},
 'deep1m':{'ANNProof':('results/work9-multidata-20261015/deep1m/ANNProof-curve-1000000/per_query.csv',64,1),'VLAH':('results/work9-multidata-20261015/deep1m/VLAH-curve-1000000/per_query.csv',100,44)},
}
# Choose NYTimes VLAH's existing >=95% default by its overall workbook provenance.
f=R/sources['nytimes256']['VLAH'][0];rr=list(csv.DictReader(f.open()));groups={}
for r in rr:
    if int(r['k'])==10:groups.setdefault((int(r['ef_search']),int(r['probes'])),[]).append(r)
target=135488622.8
key=next(k for k,rs in groups.items() if math.isclose(statistics.fmean(float(r['vo_bytes']) for r in rs),target,rel_tol=1e-9))
sources['nytimes256']['VLAH']=(sources['nytimes256']['VLAH'][0],*key)
ap=u.read(R/'experiments/work9-ablation-recall-20261022/protocol.json')
for ds in DATA:
    for v,method in [(1,'IVF-ADS'),(2,'HNSW-ADS')]:
        if ds=='glove100':p=O/ds/f'ablation-{v}'/f'variant-{v}'/'per_query.csv';parameter=chosen[v]['parameter']
        else:
            point=ap['mapping'][ds]['points'][v-1];p=Path(point['source']);parameter=point['parameter']
        sources.setdefault(ds,{})[method]=(str(p),parameter,parameter if v==1 else 1)

samples=[];summary=[]
for ds,(sheet,n,d) in DATA.items():
    for method in MODES:
        if method=='MR-tree':
            path=O/ds/method/'breakdown.csv';raw=list(csv.DictReader(path.open()));selected=raw
            original=list(csv.DictReader((R/f'results/work9-chen13-20261021/{ds}-{n}-overall/samples.csv').open()))
            original={(int(r['query_id']),int(r['repeat'])):r for r in original if int(r['k'])==10}
            for r in selected:
                old=original[int(r['query']),int(r['repeat'])]
                assert int(r['vo_bytes'])==int(old['vo_bytes']) and int(r['examined'])==int(float(old['examined'])) and r['valid']=='1'
            parameter='leaf64/fanout16';factor=1
        else:
            pathstr,ef,probes=sources[ds][method];path=Path(pathstr);path=path if path.is_absolute() else R/path
            raw=list(csv.DictReader(path.open()));selected=[r for r in raw if int(r['k'])==10 and int(r['ef_search'])==ef and int(r['probes'])==probes and 100<=int(r['query'])<120]
            parameter=f'{ef}/{probes}';factor=2 if method=='ANNProof' else 1
        assert len(selected)==60 and {(int(r['query']),int(r['repeat'])) for r in selected}=={(q,t) for q in range(100,120) for t in range(3)},(ds,method,len(selected))
        for r in selected:
            total=int(r['vo_bytes'])
            if method=='MR-tree':a=int(r['A_bytes']);recall=1.;assert a==128+32*int(r['frontier_nodes'])
            else:
                assert r['verified']=='1';recall=float(r['recall_at_k']);a=int(r['local_proof_bytes'])+32
                if method=='ANNProof':a+=4 # proof-shard count (not included in local_proof_bytes)
                if method=='VLAH':a+=int(r['route_proof_bytes'])+4 # entire authenticated routing proof + its length field
            t=total-a;assert 0<t and 0<a and t+a==total
            samples.append(dict(dataset=ds,sheet=sheet,method=method,N=n,D=d,query=int(r['query']),repeat=int(r['repeat']),parameter=parameter,recall=recall,T_raw_bytes=t,A_raw_bytes=a,total_raw_bytes=total,factor=factor,T_display_bytes=t*factor,A_display_bytes=a*factor,total_display_bytes=total*factor,source=str(path),csv_sha256=u.sha(path)))
        rr=[r for r in samples if r['dataset']==ds and r['method']==method]
        record=dict(dataset=ds,sheet=sheet,method=method,N=n,D=d,parameter=parameter,samples=60,independent_queries=20,source=str(path),csv_sha256=u.sha(path),factor=factor,status='derived from measured bytes; scaled display only' if factor!=1 else 'measured VO byte disaggregation')
        for metric in ['T_raw_bytes','A_raw_bytes','total_raw_bytes','T_display_bytes','A_display_bytes','total_display_bytes','recall']:record[metric]=statistics.fmean(r[metric] for r in rr)
        summary.append(record)
        print('BREAKDOWN',ds,method,record['T_raw_bytes']/1000,record['A_raw_bytes']/1000,record['recall'],flush=True)
with (S/'samples.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(samples[0]));writer.writeheader();writer.writerows(samples)
u.write(S/'summary.json',summary)
u.write(S/'protocol.json',dict(datasets=DATA,methods=MODES,k=10,queries=20,repeats=3,query_offset=100,KB=1000,
    definitions=dict(A='query-binding hash32 plus dedicated authentication-proof bytes and their framing; VLAH includes its complete authenticated routing proof; MR includes signed root96 and frontier digests32 each',T='all remaining serialized bytes, including vector/ID/adjacency data, MBB pruning witnesses and general control framing; T+A equals full VO; result records are included in T, not subtracted',result_only='10*(4+4*D) bytes, IDs plus raw float32 vectors; no authentication or protocol headers'),
    scale='Current overall full-scale data: SIFT1M,NYTimes289761,GloVe1183514,DEEP1M. GloVe IVF/HNSW ADS newly calibrated >=95% and measured, replacing only this new experiment prefix mismatch.',
    ANNProof='Raw measured T/A preserved; displayed T/A each multiplied by previously authorized VO factor2. Must label ANNProof* and explicitly state derived, not measured enlarged packets.',
    source_policy='Reuse verified complete20x3 query sample groups for ANNProof,VLAH, and prior SIFT/NYTimes/DEEP ablations; MR fresh VOs+verification and old-length equality; GloVe IVF/HNSW fresh full-scale VOs+verification',
    mapping='IVF-ADS=ablation variant1 Merkle+IVF; HNSW-ADS=variant2 Merkle+HNSW; no progressive quantization or verification-decoupled traversal',
    sample_note='SIFT overall historically uses100 queries; this experiment uniformly uses first20 (100..119), so means/recalls need not equal old100-query overall means'))
u.write(S/'FINISHED.json',dict(passed=True,points=20,accounted_query_samples=1200,fresh_mr_authenticated_queries=240,fresh_glove_ads_authenticated_queries=120))
