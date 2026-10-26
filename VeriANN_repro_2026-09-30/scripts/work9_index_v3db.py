"""Fresh author-compatible single-thread IVF/PQ training + snapshot roots."""
import os
for key in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','RAYON_NUM_THREADS']:os.environ[key]='1'
import argparse,json,sys,time,resource
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'V3DB-26VLDB/scripts'))
from work9_calibrate import vectors,bounded_rebalance,train_pq_subspaces
from work9_measure import list_records
from ivf_pq.util.kmeans import faiss_kmeans_with_ids
from zk_IVF_PQ.zk_IVF_PQ import py_work9_cluster_root,py_work9_snapshot_roots
import numpy as np,faiss
from work9_index_study import DATA,OUT,write,sha
resource.setrlimit(resource.RLIMIT_AS,(50*1024**3,50*1024**3))
faiss.omp_set_num_threads(1)
assert faiss.get_num_gpus()==0,'This single-thread CPU comparison requires the CPU Faiss environment.'
selected=json.loads((ROOT/'experiments/work9-v3db-20261016/selected-models.json').read_text())
parser=argparse.ArgumentParser();parser.add_argument('--dataset',required=True,choices=list(DATA));args=parser.parse_args()
for ds,(title,n,d,_,_) in DATA.items():
    if ds!=args.dataset:continue
    out=OUT/ds/'V3DB'
    if (out/'measurement.json').exists():continue
    if out.exists():raise RuntimeError('Inspect incomplete run '+str(out))
    out.mkdir(parents=True)
    old=ROOT/'V3DB-26VLDB/work9-cache'/selected[ds]['model']
    cfg=json.loads((old/'model.json').read_text())['config'];M,K,L,cap=[cfg[k] for k in ['M','K','n_list','capacity']]
    base=vectors(ROOT/'dataset'/ds/'base.fvecs',n)
    print('START V3DB',ds,flush=True);started=time.perf_counter()
    vmin,vmax=float(base.min()),float(base.max())
    scaled=np.lib.format.open_memmap(out/'scaled-training-only.npy',mode='w+',dtype=np.int64,shape=(n,d))
    for lo in range(0,n,8192):scaled[lo:lo+8192]=np.clip(np.rint((np.asarray(base[lo:lo+8192],dtype=np.float64)-vmin)*(65535/(vmax-vmin))),0,65535).astype(np.int64)
    scaled.flush();scale_s=time.perf_counter()-started
    print('SCALE_DONE',ds,scale_s,flush=True)
    t=time.perf_counter();centers,groups,labels=faiss_kmeans_with_ids(scaled,L,niter=25,random_state=0);centers=np.rint(centers).astype(np.int64);coarse_s=time.perf_counter()-t
    print('COARSE_DONE',ds,coarse_s,flush=True)
    bounded_rebalance();import ivf_pq.zk
    t=time.perf_counter();labels,groups,moved=ivf_pq.zk.rebalance_clusters(scaled,centers,labels,cap);rebalance_s=time.perf_counter()-t
    print('REBALANCE_DONE',ds,rebalance_s,'PQ_START',M,K,flush=True)
    t=time.perf_counter();books,codes=train_pq_subspaces(scaled,centers,labels,M,K,workers=1);books=np.rint(books).astype(np.int64);pq_s=time.perf_counter()-t
    index_s=time.perf_counter()-started
    print('PQ_DONE',ds,pq_s,'SNAPSHOT_ROOTS_START',flush=True)
    t=time.perf_counter();roots=[]
    for idx in range(L):
        vc,valid,items=list_records(groups,codes,idx,cap,M)
        roots.append(py_work9_cluster_root(idx,valid,items,vc))
    root,cb=py_work9_snapshot_roots(centers,books,roots);ads_s=time.perf_counter()-t
    # Actual array dtypes used by the adapter. No hypothetical bit packing.
    # Groups supply list membership and item IDs; labels are redundant at serve time.
    search=out/'search.snapshot'
    with search.open('wb') as f:
        np.savez(f,centers=centers,books=books,codes=codes,group_offsets=np.cumsum([0]+[len(groups[i]) for i in range(L)],dtype=np.int64),group_ids=np.concatenate([groups[i] for i in range(L)]).astype(np.int64),parameters=np.array([n,d,L,M,K,cap],dtype=np.int64),scale=np.array([vmin,vmax],dtype=np.float64))
    auth=out/'auth.snapshot'
    with auth.open('wb') as f:np.savez(f,list_roots=np.asarray(roots,dtype=np.uint64),roots=np.asarray([root,cb],dtype=np.uint64))
    record=dict(dataset=ds,title=title,method='V3DB',N=n,D=d,M=M,K=K,n_list=L,capacity=cap,threads=1,repeats=1,status='measured',index_seconds=index_s,ads_seconds=ads_s,construct_seconds=index_s+ads_s,scale_seconds=scale_s,coarse_seconds=coarse_s,rebalance_seconds=rebalance_s,pq_seconds=pq_s,search_bytes=search.stat().st_size,auth_bytes=auth.stat().st_size,total_bytes=search.stat().st_size+auth.stat().st_size,peak_rss_KiB=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,relocated=int(moved),config=cfg,search_sha256=sha(search),auth_sha256=sha(auth),script_sha256=sha(__file__),source='fresh single-thread rebuild with author training and existing exact bounded-memory rebalance adapter; excludes query circuits/proving and training-only scaled vectors')
    write(out/'measurement.json',record);print('DONE V3DB',ds,record['construct_seconds'],record['total_bytes'],flush=True)
    del scaled,base,books,codes,centers,labels,groups
