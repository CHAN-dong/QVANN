"""Fresh NYTimes ANNProof experiment; one graph across the recall sweep."""
import csv,json,shutil,statistics,time
from collections import defaultdict
from pathlib import Path
import work9_resource_matched as u
R=u.ROOT;S=R/'experiments/work9-nytimes-annproof-full-20261021';O=R/'results/work9-nytimes-annproof-full-20261021'
u.STUDY=S;u.RESULTS=O;S.mkdir(parents=True,exist_ok=True)
SOURCE=R.parent/'work9.xlsx';BACKUP=S/'work9_before_full_retest.xlsx'
TARGETS=[.15,.20,.25,.30,.35,.40,.45,.50,.55,.60,.65,.70,.75,.80,.85,.90,.95,.97,.98,.99]
def common(n,m,efc,cache):return ['--dataset',R/'dataset/nytimes256','--base',n,'--M',m,'--ef-construction',efc,'--cache',cache,'--queries',20,'--query-offset',100,'--build-threads',1,'--probes',1]
def pilot(tag,n,m,efc,grid,cache):
 out=O/'pilots'/tag;record=S/'pilots'/(tag+'.json')
 if record.exists():return u.read(record)['points']
 cmd=[R/'build/multidata_pilot_ANNProof',*common(n,m,efc,cache),'--output',out,'--k',10,'--ef-search',','.join(map(str,grid))]
 u.command(cmd,S/'logs'/(tag+'.log'),bounded=True,timeout=7200)
 groups=defaultdict(list)
 for r in csv.DictReader((out/'pilot.csv').open()):groups[int(r['ef_search'])].append(r)
 points=[]
 for ef,rr in sorted(groups.items()):
  assert len(rr)==20 and {int(r['query']) for r in rr}==set(range(100,120))
  points.append(dict(ef=ef,recall=statistics.fmean(float(r['recall_at_k']) for r in rr),valid=all(r['valid']=='1' for r in rr)))
 u.write(record,dict(N=n,M=m,efc=efc,points=points,csv_sha256=u.sha(out/'pilot.csv'),command=list(map(str,cmd))))
 valid=[p['recall'] for p in points if p['valid']];print('PILOT_RANGE',tag,min(valid),max(valid),flush=True)
 return points
def measure(tag,n,m,efc,cache,efs,ks=[10]):
 out=O/'measurements'/tag;record=out/'measurement.json'
 if record.exists():return u.read(record)['points']
 cmd=[R/'ANNProof-24FGCS/build/annproof',*common(n,m,efc,cache),'--output',out,'--ef-search',','.join(map(str,efs)),'--k',','.join(map(str,ks)),'--repeats',3,'--metric','l2','--proof','multiproof']
 u.command(cmd,S/'logs'/(tag+'.log'),bounded=True,timeout=21600)
 rr=list(csv.DictReader((out/'per_query.csv').open()));attacks=list(csv.DictReader((out/'tamper_tests.csv').open()))
 assert len(attacks)==8 and all(r['rejected']=='1' for r in attacks) and all(r['verified']=='1' for r in rr)
 assert (out/'query0.vo').stat().st_size==int(rr[0]['vo_bytes'])
 (out/'PASS.txt').write_text('All queries verified; 8 tamper attacks rejected; actual wire-size checked.\n')
 points=u.aggregate(out)
 for p in points:p.update(N=n,M=m,efc=efc,threads=1)
 u.write(record,dict(points=points,command=list(map(str,cmd)),tamper_rejections=8,wire_size_checked=True))
 print('MEASURED',tag,[(p['k'],p['ef'],p['recall']) for p in points],flush=True)
 return points
def main():
 if not BACKUP.exists():shutil.copy2(SOURCE,BACKUP)
 assert u.sha(SOURCE)==u.sha(BACKUP),'Workbook changed while experiments running'
 u.write(S/'protocol.json',dict(N=289761,D=256,queries=20,repeats=3,query_offset=100,threads=1,targets=TARGETS,ks=[1,5,10,20,50,100],size=[100000],curve='one fixed graph across every recall target; efSearch only; no graph switching or trend fitting',default='M16/efc200, recalibrate efSearch to at least .95; K sweep uses same budget',index='fresh single-thread M16/efc200 graph plus ADS build and serialized serving snapshot',factors={'query_ms':1.2,'vo_kb':2,'verify_ms':1.5},source_sha256={str(p.relative_to(R)):u.sha(p) for p in [R/'ANNProof-24FGCS/annproof.cpp',R/'src/paper_repro_common.hpp',R/'src/verifiable_ann_common.hpp']},native_sha256=u.sha(R/'ANNProof-24FGCS/build/annproof')))
 grid=list(range(10,129))+list(range(136,513,8))+list(range(544,2049,32))+[3072,4096,6144,8192]
 choice=S/'curve-choice.json'
 if not choice.exists():
  for m,efc in [(8,8),(6,12),(4,40)]:
   tag=f'curve-M{m}-efc{efc}';cache=S/'cache'/tag;pp=pilot(tag,289761,m,efc,grid,cache);valid=[p for p in pp if p['valid']]
   if min(p['recall'] for p in valid)>.425:continue
   if max(p['recall'] for p in valid)<.99:
    for e in [16384,32768,65536,131072,289761]:
     pp+=pilot(tag+'-extend-'+str(e),289761,m,efc,[e],cache)
     if any(p['valid'] and p['recall']>=.99 for p in pp):break
   u.write(choice,dict(M=m,efc=efc,cache=str(cache),points=pp));break
  else:raise RuntimeError('No graph supplies the required low recall; inspect pilots')
 ch=u.read(choice);pp=[p for p in ch['points'] if p['valid']];selected=[]
 for t in TARGETS:
  good=[p for p in pp if p['recall']>=t-1e-12]
  selected.append(dict(target=t,choice=min(good,key=lambda p:p['ef']) if good else None))
 u.write(S/'curve-selection.json',selected)
 measured=measure('fixed-graph-recall',289761,ch['M'],ch['efc'],ch['cache'],sorted({s['choice']['ef'] for s in selected if s['choice']}))
 # Default graph: construct once freshly, then use that exact graph for calibration
 # and K/overall measurements. No old cache or old timing is reused.
 ix=O/'fresh-default-index';meta=ix/'measurement.json'
 if not meta.exists():
  ix.mkdir(parents=True,exist_ok=True);binary=R/'experiments/work9-index-20261017/ANNProof'
  u.command([binary,*common(289761,16,200,ix/'cache'),'--output',ix],S/'logs/fresh-default-index.log',bounded=True,timeout=7200)
  p=u.read(ix/'offline.json');assert p['total_bytes']==(ix/'serving.snapshot').stat().st_size
  p.update(snapshot_sha256=u.sha(ix/'serving.snapshot'),binary_sha256=u.sha(binary),M=16,efc=200,N=289761,threads=1)
  u.write(meta,p)
 runtime=S/'cache/default';runtime.mkdir(parents=True,exist_ok=True)
 # Derive the native cache identity from the actual data fingerprint in a prior
 # manifest; hash is validated against the new native measurement below.
 manifest=u.read(R/'results/work9-nytimes-low-retest-20261021/manifest.json')
 cachefile=runtime/(manifest['dataset_sha256'][:16]+'_M16_efc200.index')
 if not cachefile.exists():shutil.copy2(ix/'cache/global.index',cachefile)
 pp=pilot('default-M16-efc200',289761,16,200,list(range(10,129))+list(range(136,769,8))+[1024,1536,2048],runtime)
 ef=min(p['ef'] for p in pp if p['valid'] and p['recall']>=.95-1e-12)
 default=measure('default-and-k',289761,16,200,runtime,[ef],[1,5,10,20,50,100])
 assert u.read(O/'measurements/default-and-k/manifest.json')['dataset_sha256']==manifest['dataset_sha256']
 smallcache=S/'cache/size100k'
 pp=pilot('size100k-M16-efc200',100000,16,200,list(range(10,129))+list(range(136,769,8))+[1024,1536,2048],smallcache)
 e=min(p['ef'] for p in pp if p['valid'] and p['recall']>=.95-1e-12)
 size=measure('size100k',100000,16,200,smallcache,[e])
 u.write(S/'FINISHED.json',dict(curve_graph=ch,curve_selection=selected,curve_measurements=measured,default_k=default,size=size,index=u.read(meta),backup_sha256=u.sha(BACKUP)))
 print('ALL_EXPERIMENTS_FINISHED',flush=True)
if __name__=='__main__':main()
