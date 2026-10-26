"""Author IVF-PQ training and actual recall scan on work9's full datasets.

No proof-time estimates are produced here. Results are search calibration only.
"""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[2]
REPRO = ROOT / 'V3DB-26VLDB'
STUDY = ROOT / 'experiments/work9-v3db-20261016'
CACHE = REPRO / 'work9-cache'
sys.path.insert(0, str(REPRO / 'upstream'))
import numpy as np
import faiss
from ivf_pq import rescale_query
from ivf_pq.zk import ivf_pq_learn
from ivf_pq import rebalance as rebalance_module

DATA = {'sift1m': ('SIFT1M', 1000000, 128, 1024, 8),
        'gist1m': ('GIST1M', 1000000, 960, 512, 8),
        'glove100': ('GloVe-100', 1183514, 100, 1024, 10),
        'deep1m': ('DEEP1M', 1000000, 96, 1024, 8),
        'nytimes256': ('NYTimes-256', 289761, 256, 512, 8)}


def event(**fields):
    fields['time'] = datetime.now().astimezone().isoformat()
    STUDY.mkdir(parents=True, exist_ok=True)
    with (STUDY / 'events.jsonl').open('a') as f:
        f.write(json.dumps(fields) + '\n')
    print(json.dumps(fields), flush=True)


def vectors(path, count=None):
    if not path.exists():
        path = path.with_name('sift_' + path.name)
    with path.open('rb') as f:
        d = np.fromfile(f, '<i4', 1).item()
    mm = np.memmap(path, '<f4', 'r').reshape(-1, d + 1)
    return mm[:count, 1:] if count else mm[:, 1:]


def bounded_rebalance():
    # Execute the author's unchanged algorithm, replacing ONLY the temporary
    # distance tensor allocation with exact-integer float64 matrix distances.
    import inspect
    source = inspect.getsource(rebalance_module.rebalance_clusters)
    old = ('diff_free = X[:, None, :] - centers_free[None, :, :]\n'
           '            dist_free = np.sum(diff_free * diff_free, axis=2)')
    new = ('assert np.all(X == np.rint(X)) and np.all(centers_free == np.rint(centers_free))\n'
           '            assert max(float(np.max(np.abs(X))), float(np.max(np.abs(centers_free)))) ** 2 * X.shape[1] * 4 < 2**53\n'
           '            dist_free = np.empty((X.shape[0], centers_free.shape[0]), dtype=np.float64)\n'
           '            norm_free = np.sum(centers_free * centers_free, axis=1)\n'
           '            for block_start in range(0, X.shape[0], 1024):\n'
           '                block = X[block_start:block_start + 1024]\n'
           '                dist_free[block_start:block_start + 1024] = np.sum(block * block, axis=1)[:, None] + norm_free[None, :] - 2 * (block @ centers_free.T)')
    assert old in source
    namespace = dict(np=np)
    exec(compile(source.replace(old, new), '<author-rebalance-row-chunks>', 'exec'), namespace)
    import ivf_pq.zk
    ivf_pq.zk.rebalance_clusters = namespace['rebalance_clusters']


def score_all_queries(queries,centers,books,codes,labels):
    """Exact integer PQ scores in a batch; float64 products stay below 2**53."""
    count,d=queries.shape
    M=books.shape[0]
    n=len(codes)
    result=np.empty((count,n),dtype=np.int64)
    q=queries.astype(np.float64)
    qnorm=np.sum(q*q,axis=1)
    for lo in range(0,n,4096):
        reconstructed=books[np.arange(M)[None,:],codes[lo:lo+4096]].reshape(-1,d)
        approx=centers[labels[lo:lo+4096]]+reconstructed
        bound=max(float(np.max(np.abs(approx))),float(np.max(np.abs(queries))))
        assert 4*bound*bound*d<2**53
        x=approx.astype(np.float64)
        distances=qnorm[:,None]+np.sum(x*x,axis=1)[None,:]-2*(q@x.T)
        assert np.all(distances==np.rint(distances))
        result[:,lo:lo+4096]=distances.astype(np.int64)
    return result


def train_pq_subspaces(scaled,centers,labels,M,K,workers=8,niter=25):
    """Independent author KMeans calls, each using one CPU thread."""
    from ivf_pq.util.kmeans import faiss_kmeans_with_ids
    d=scaled.shape[1]//M
    def train(lo):
        faiss.omp_set_num_threads(1)
        residual=scaled[:,lo:lo+d]-centers[labels,lo:lo+d]
        book,_,code=faiss_kmeans_with_ids(residual,K,niter=niter,random_state=0)
        return book,code
    books=[];codes=[]
    with ThreadPoolExecutor(max_workers=min(workers,M)) as pool:
        for book,code in pool.map(train,range(0,scaled.shape[1],d)):
            books.append(book);codes.append(code)
    return np.asarray(books),np.asarray(codes).T.copy()


def prepare(dataset, n=None, source_name=None, pq_m=None, pq_k=256):
    title, full_n, d, lists, M = DATA[dataset]
    n = n or full_n
    default_m=M
    M=pq_m or M
    assert d%M==0
    folder = CACHE / (f'{source_name or dataset}-{n}'+(f'-M{M}' if M!=default_m else '')+(f'-K{pq_k}' if pq_k!=256 else ''))
    folder.mkdir(parents=True, exist_ok=True)
    path = ROOT / 'dataset' / (source_name or dataset)
    base = vectors(path / 'base.fvecs', n)
    query = vectors(path / 'query.fvecs')[100:120].copy()
    assert len(base) == n and base.shape[1] == d and len(query) == 20
    config = dict(dataset=dataset, source=source_name or dataset, N=n, D=d, n_list=lists,
                  M=M, K=pq_k, n_iter=25, seed=0, capacity=1 << (int(np.ceil(2*n/lists)) - 1).bit_length(),
                  query_offset=100, query_count=20, scale=65536,
                  rebalance='author algorithm with identical row-chunk distances')
    meta = folder / 'model.json'
    if meta.exists():
        stored = json.loads(meta.read_text())
        assert stored['config'] == config
        return folder, base, query, stored
    faiss.omp_set_num_threads(16)
    event(action='train_start', **config)
    start = time.perf_counter()
    # Same global affine scale/clipping/rounding as author rescale_database,
    # streamed to disk to avoid several simultaneous N*D float64 temporaries.
    vmin, vmax = float(base.min()), float(base.max())
    coarse_folder=CACHE/f'{source_name or dataset}-{n}'
    if (M!=default_m or pq_k!=256) and (coarse_folder/'model.json').exists():
        coarse=json.loads((coarse_folder/'model.json').read_text())
        scaled=np.load(coarse_folder/'scaled.npy',mmap_mode='r')
        labels=np.load(coarse_folder/'labels.npy')
        centers=np.load(coarse_folder/'centers.npy')
        groups_file=np.load(coarse_folder/'groups.npz')
        groups={int(key):groups_file[key] for key in groups_file.files}
        moved=coarse['relocated']
        books,codes=train_pq_subspaces(scaled,centers,labels,M,pq_k)
        for name,value in [('labels',labels),('centers',centers),('books',books),('codes',codes)]:
            np.save(folder/f'{name}.npy',value)
        np.savez(folder/'groups.npz',**{str(k):v for k,v in groups.items()})
        result=dict(config=config,vmin=coarse['vmin'],vmax=coarse['vmax'],relocated=int(moved),
                    train_seconds=time.perf_counter()-start,coarse_reused=str(coarse_folder),pq_workers=8,pq_threads_per_worker=1)
        meta.write_text(json.dumps(result,indent=2)+'\n')
        event(action='train_done',path=str(folder),**result)
        return folder,base,query,result
    scaled = np.lib.format.open_memmap(folder/'scaled.npy', mode='w+', dtype=np.int64, shape=base.shape)
    for lo in range(0, n, 8192):
        block = np.asarray(base[lo:lo+8192], dtype=np.float64)
        scale=65535/(vmax-vmin)
        scaled[lo:lo+8192] = np.clip(np.rint((block-vmin)*scale), 0, 65535).astype(np.int64)
    scaled.flush()
    bounded_rebalance()
    from ivf_pq.util.kmeans import faiss_kmeans_with_ids
    import ivf_pq.zk
    centers,groups,labels=faiss_kmeans_with_ids(scaled,lists,niter=25,random_state=0)
    centers=np.rint(centers).astype(np.int64)
    labels,groups,moved=ivf_pq.zk.rebalance_clusters(scaled,centers,labels,config['capacity'])
    books,codes=train_pq_subspaces(scaled,centers,labels,M,pq_k)
    assert all(len(g) <= config['capacity'] for g in groups.values())
    for name, value in [('labels',labels),('centers',centers),('books',books),('codes',codes)]:
        np.save(folder/f'{name}.npy', value)
    np.savez(folder/'groups.npz', **{str(k):v for k,v in groups.items()})
    result = dict(config=config, vmin=vmin, vmax=vmax, relocated=int(moved),
                  train_seconds=time.perf_counter()-start,pq_workers=8,pq_threads_per_worker=1)
    meta.write_text(json.dumps(result, indent=2)+'\n')
    event(action='train_done', path=str(folder), **result)
    return folder, base, query, result


def calibrate(dataset, n=None, source_name=None, pq_m=None, pq_k=256):
    folder, base, queries, meta = prepare(dataset, n, source_name,pq_m,pq_k)
    output = folder/'calibration.json'
    if output.exists():
        return json.loads(output.read_text())
    cfg = meta['config']
    n, d, lists, M, K = [cfg[k] for k in ['N','D','n_list','M','K']]
    centers = np.load(folder/'centers.npy')
    books = np.rint(np.load(folder/'books.npy')).astype(np.int64)
    codes = np.load(folder/'codes.npy', mmap_mode='r')
    labels = np.load(folder/'labels.npy', mmap_mode='r')
    ks = [1,5,10,20,50,100]
    probes = sorted({1,2,4,8,16,32,64,128,256,512,lists} & set(range(1,lists+1)))
    all_records = []
    faiss.omp_set_num_threads(1)
    event(action='calibration_start', **cfg)
    scaled_queries=np.stack([rescale_query(q,65536,meta['vmin'],meta['vmax']) for q in queries])
    candidate_scores=score_all_queries(scaled_queries,centers,books,codes,labels)
    truth_folder=CACHE/f"{cfg['source']}-{n}"/'exact-ground-truth'
    truth_folder.mkdir(parents=True,exist_ok=True)
    for qi, query in enumerate(queries):
        # Exact L2 ground truth, blockwise float64 on original prepared vectors.
        truth_file=truth_folder/f'query-{100+qi}.npy'
        if truth_file.exists():
            gt=np.load(truth_file)
        else:
            distances = np.empty(n, dtype=np.float64)
            for lo in range(0,n,8192):
                block = np.asarray(base[lo:lo+8192], dtype=np.float64)-query
                distances[lo:lo+8192] = np.sum(block*block, axis=1)
            ids = np.argpartition(distances, 99)[:100]
            gt = ids[np.lexsort((ids,distances[ids]))]
            np.save(truth_file,gt)
        q = rescale_query(query, 65536, meta['vmin'], meta['vmax'])
        coarse_dist = np.sum((centers-q)**2,axis=1)
        order = np.argsort(coarse_dist,kind='stable')
        ranks = np.empty(lists, dtype=np.int64); ranks[order] = np.arange(lists)
        candidate_dist=candidate_scores[qi]
        for probe in probes:
            ids = np.flatnonzero(ranks[labels] < probe)
            top = ids[np.argpartition(candidate_dist[ids], min(99,len(ids)-1))[:100]]
            top = top[np.lexsort((top,candidate_dist[top]))]
            for k in ks:
                recall = len(set(top[:k].tolist()) & set(gt[:k].tolist()))/k
                all_records.append(dict(query=100+qi,k=k,n_probe=probe,recall=recall,
                                        returned=top[:k].tolist()))
        if qi%5==0:
            event(action='calibration_query', dataset=dataset,N=n,query=100+qi)
    grouped=[]
    for probe in probes:
        for k in ks:
            r=[x['recall'] for x in all_records if x['n_probe']==probe and x['k']==k]
            grouped.append(dict(n_probe=probe,k=k,recall=float(np.mean(r))))
    result=dict(model=meta,points=grouped,per_query=all_records,
                metric='set overlap Recall@k on original vectors; query IDs100..119',
                role='calibration only; not proof/query/verification timing')
    output.write_text(json.dumps(result,indent=2)+'\n')
    event(action='calibration_done',dataset=dataset,N=n,
          recall10=[x for x in grouped if x['k']==10])
    return result


if __name__ == '__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--dataset', choices=list(DATA)+['all'], default='all')
    p.add_argument('--N',type=int)
    p.add_argument('--source')
    p.add_argument('--M',type=int)
    p.add_argument('--K',type=int,default=256)
    a=p.parse_args()
    for key in DATA if a.dataset=='all' else [a.dataset]:
        calibrate(key,a.N,a.source,a.M,a.K)
