"""Recheck the SIFT/BIGANN size-panel point on the evaluation query IDs."""
import csv,json,statistics
from pathlib import Path
import work9_resource_matched as u
R=u.ROOT; S=R/'experiments/work9-monotonicity-audit-20261021'
u.STUDY=S;u.RESULTS=R/'results/work9-monotonicity-audit-20261021'
S.mkdir(parents=True,exist_ok=True)
common=['--dataset',R/'dataset/bigann10m','--base',1000000,'--cache',R/'ANNProof-24FGCS/cache','--queries',20,'--query-offset',100,'--build-threads',1,'--M',8,'--ef-construction',100,'--k',10,'--probes',1]
pilot=u.RESULTS/'sift-size-1m-pilot'
if not (pilot/'pilot.csv').exists():
 u.command([R/'build/multidata_pilot_ANNProof',*common,'--output',pilot,'--ef-search',','.join(map(str,range(50,401,10)))],S/'pilot.log',bounded=True,timeout=3600)
groups={}
for r in csv.DictReader((pilot/'pilot.csv').open()):groups.setdefault(int(r['ef_search']),[]).append(r)
good=[e for e,rr in groups.items() if len(rr)==20 and all(r['valid']=='1' for r in rr) and statistics.fmean(float(r['recall_at_k']) for r in rr)>=.95-1e-12]
assert good
ef=min(good);out=u.RESULTS/'sift-size-1m-95'
if not (out/'per_query.csv').exists():
 u.command([R/'ANNProof-24FGCS/build/annproof',*common,'--output',out,'--ef-search',ef,'--repeats',3,'--metric','l2','--proof','multiproof'],S/'measure.log',bounded=True,timeout=3600)
rr=list(csv.DictReader((out/'per_query.csv').open()));aa=list(csv.DictReader((out/'tamper_tests.csv').open()))
assert len(rr)==60 and all(r['verified']=='1' for r in rr) and len(aa)==8 and all(r['rejected']=='1' for r in aa)
p=dict(ef=ef,recall=statistics.fmean(float(r['recall_at_k']) for r in rr),query_ms=statistics.fmean(float(r['search_ms'])+float(r['vo_generation_ms']) for r in rr),verify_ms=statistics.fmean(float(r['verification_ms']) for r in rr),vo_kb=statistics.fmean(int(r['vo_bytes'])/1000 for r in rr),samples=60,csv=str(out/'per_query.csv'),csv_sha256=u.sha(out/'per_query.csv'),query_offset=100,N=1000000,dataset='bigann10m',M=8,efc=100,threads=1,tamper_rejections=8)
assert p['recall']>=.95-1e-12
u.write(S/'sift-size-1m-corrected.json',p)
print(json.dumps(p),flush=True)
