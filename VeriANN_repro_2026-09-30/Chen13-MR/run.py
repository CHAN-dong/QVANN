"""Measured-only, checkpointed Chen13-MR high-dimensional experiment.
Run with the project's WSL Python; one native process and one thread at a time.
"""
import argparse, csv, hashlib, json, os, resource, shutil, statistics, subprocess, time
from pathlib import Path

HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
OUT=ROOT/'results/work9-chen13-20261021'
STUDY=ROOT/'experiments/work9-chen13-20261021'
DATA={'sift1m':('SIFT1M',1_000_000), 'nytimes256':('NYTimes-256',289_761),
      'glove100':('GloVe-100',1_183_514), 'deep1m':('DEEP1M',1_000_000)}
KS=[1,5,10,20,50,100]
SIZES=[100_000,500_000,1_000_000,5_000_000,10_000_000]

def write(p,x):
 p.parent.mkdir(parents=True,exist_ok=True)
 t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n');t.replace(p)
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def limits():
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
 resource.setrlimit(resource.RLIMIT_AS,(50*1024**3,50*1024**3))
def env():
 e=os.environ.copy()
 for k in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','RAYON_NUM_THREADS']:e[k]='1'
 return e
def job(ds,n,ks,kind='overall',nq=20,reps=3,basepath=None,querypath=None):
 name=f'{ds}-{n}-{kind}'
 out=OUT/name;out.mkdir(parents=True,exist_ok=True)
 record=out/'measurement.json'
 if record.exists():
  r=json.loads(record.read_text())
  assert r['source_sha256']==sha(HERE/'mr.cpp') and r['binary_sha256']==sha(HERE/'mr'),'Archive old results before changing implementation'
  return r
 base=basepath or ROOT/'dataset'/ds/'sift_base.fvecs'
 query=querypath or ROOT/'dataset'/ds/'sift_query.fvecs'
 cmd=[str(HERE/'mr'),str(base),str(query),str(n),str(out),','.join(map(str,ks)),str(nq),str(reps),'64']
 write(out/'started.json',dict(command=cmd,time=time.strftime('%FT%T%z'),threads=1,address_space_limit_GiB=50))
 print('START',name,flush=True);t=time.time()
 try:
  with (out/'run.log').open('w') as log:
   p=subprocess.run(cmd,cwd=ROOT,env=env(),preexec_fn=limits,stdout=log,stderr=subprocess.STDOUT,timeout=6*3600)
  if p.returncode:raise RuntimeError(f'native exit {p.returncode}: '+(out/'run.log').read_text()[-2000:])
  native=json.loads((out/'native.json').read_text())
  with (out/'samples.csv').open() as f:rows=list(csv.DictReader(f))
  assert len(rows)==len(ks)*nq*reps
  points=[]
  for k in ks:
   rr=[r for r in rows if int(r['k'])==k]
   assert len(rr)==nq*reps and all(r['valid']=='1' and float(r['recall'])==1 for r in rr)
   a=dict(k=k,samples=len(rr),recall=1.0)
   for key in ['query_ms','search_ms','encode_ms','verify_ms','vo_bytes','examined','expanded']:
    values=[float(r[key]) for r in rr];a[key]=statistics.mean(values)
    if key.endswith('_ms'):a[key+'_stdev']=statistics.stdev(values) if len(values)>1 else 0
   a['vo_kb']=a['vo_bytes']/1000;points.append(a)
  r=dict(status='measured',dataset=ds,N=n,kind=kind,native=native,points=points,command=cmd,
         wall_seconds=time.time()-t,source_sha256=sha(HERE/'mr.cpp'),binary_sha256=sha(HERE/'mr'),
         samples_sha256=sha(out/'samples.csv'),snapshot_sha256=sha(out/'serving.snapshot'),
         query_ids=list(range(100,100+nq)),repeats=reps,threads=1,
         metric='squared L2 on the existing float32 vectors; float64 accumulation; ties by ID')
  write(record,r);print('DONE',name,[(p['k'],round(p['query_ms'],2),round(p['vo_kb'],2)) for p in points],flush=True)
  return r
 except BaseException as exc:
  write(out/'failure.json',dict(error=str(exc),elapsed=time.time()-t,command=cmd))
  raise

def smoke():
 import numpy as np
 rng=np.random.default_rng(20261021)
 def save(p,x):
  x=np.asarray(x,dtype='<f4');y=np.empty((len(x),x.shape[1]+1),dtype='<f4');y[:,0].view('<i4')[:]=x.shape[1];y[:,1:]=x;y.tofile(p)
 cases=[]
 for d in [2,100,128,256]:
  b=rng.normal(size=(513,d)).astype('f4');b[1]=b[0];b[2]=0
  q=rng.normal(size=(120,d)).astype('f4');q[100]=b[0];q[101]=0
  if d==2:b[:256]-=100;b[256:]+=100;q[102]=b[300]
  p=STUDY/f'smoke-{d}';p.mkdir(parents=True,exist_ok=True);save(p/'base.fvecs',b);save(p/'query.fvecs',q)
  r=job('smoke'+str(d),len(b),[1,10,100],kind='correctness',nq=20,reps=1,basepath=p/'base.fvecs',querypath=p/'query.fvecs');cases.append(r)
 write(STUDY/'smoke.json',dict(passed=True,cases=cases))

def run():
 STUDY.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
 import fcntl
 lock=(STUDY/'runner.lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 backup=STUDY/'work9_before_chen13.xlsx'
 if not backup.exists():shutil.copy2(ROOT.parent/'work9.xlsx',backup)
 write(STUDY/'protocol.json',dict(method='Chen13-MR (plaintext high-dimensional adaptation)',
       paper='Chen, Hu, Xu, PVLDB 7(1):49-60, 2013, section 5.1.1',
       datasets=DATA,k_values=KS,size_values=SIZES,query_ids=list(range(100,120)),repeats=3,threads=1,
       leaf_capacity=64,fanout=16,packing='recursive spatial packing; not author R*-tree implementation',
       privacy='none; no Paillier, PPB, PLB, confidentiality or power diagram',
       exact=True,curve='single exact 100% point; no tunable recall; do not duplicate under lower target recalls',
       query_time='best-first exact search + construction/serialization of actual complete VO; no disk/network transfer',
       verify_time='VO decoding + public-key signature + Merkle reconstruction + all completeness/ranking checks',
       vo='full actual binary VO including root, 64-byte signature, query commitment and answer IDs; KB=1000 bytes',
       build='single fresh in-memory spatial index + authenticated digests + signature; snapshot I/O separately reported',
       scale_source='SIFT size: BIGANN 10M prefix; DEEP size: same DEEP family; recompute exact oracle for every N'))
 smoke()
 for ds,(_,n) in DATA.items():job(ds,n,KS if ds in ['sift1m','deep1m'] else [10])
 for ds in ['sift1m','deep1m']:
  for n in SIZES:
   source='bigann10m' if ds=='sift1m' else ('deep10m' if n>1_000_000 else 'deep1m')
   if ds=='deep1m' and n==1_000_000:
    main=OUT/f'{ds}-{n}-overall'/'measurement.json'
    write(STUDY/'deep-size-1m-reuse.json',dict(source=str(main),reason='same data/query/parameters; reuse measured k=10 point'))
    continue
   job(source,n,[10],kind='size')
 write(STUDY/'RUN_FINISHED.json',dict(status='all measured',time=time.strftime('%FT%T%z')))

if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('action',choices=['run','smoke','pilot']);args=a.parse_args()
 if args.action=='run':run()
 elif args.action=='smoke':smoke()
 else:job('sift1m',1_000_000,[10],kind='pilot',nq=1,reps=1)
