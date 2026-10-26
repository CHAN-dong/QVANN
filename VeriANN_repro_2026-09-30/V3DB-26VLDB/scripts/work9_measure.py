"""Measure author multiset proofs against fixed snapshots; save real wire VOs."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import sys
import time

from work9_calibrate import ROOT, REPRO, STUDY, CACHE, DATA, event, vectors
import numpy as np
from ivf_pq import rescale_query
from zk_IVF_PQ.zk_IVF_PQ import (py_set_based_with_merkle, py_set_based_gate,
    py_work9_cluster_root, py_work9_snapshot_roots, py_work9_verify_wire)

RESULTS = Path(os.environ.get('V3DB_RESULTS_DIR',str(ROOT/'results/work9-v3db-20261016')))
P = 2**64-2**32+1


def load_model(folder):
    meta=json.loads((folder/'model.json').read_text())
    cfg=meta['config']
    centers=np.load(folder/'centers.npy').astype(np.int64)
    books=np.rint(np.load(folder/'books.npy')).astype(np.int64)
    codes=np.load(folder/'codes.npy',mmap_mode='r')
    groups_file=np.load(folder/'groups.npz')
    groups={int(k):groups_file[k] for k in groups_file.files}
    return meta,centers,books,codes,groups


def list_records(groups,codes,idx,capacity,M):
    ids=groups[int(idx)]
    assert len(ids)<=capacity
    vc=np.zeros((capacity,M),dtype=np.int64)
    valid=np.zeros(capacity,dtype=np.int64)
    items=np.zeros(capacity,dtype=np.int64)
    vc[:len(ids)]=codes[ids]
    valid[:len(ids)]=1
    items[:len(ids)]=ids
    return vc,valid,items


def snapshot(folder,model):
    meta,centers,books,codes,groups=model
    cfg=meta['config']
    file=folder/'snapshot.json'
    if file.exists():return json.loads(file.read_text())
    roots=[]
    start=time.perf_counter()
    for idx in range(cfg['n_list']):
        vc,valid,items=list_records(groups,codes,idx,cfg['capacity'],cfg['M'])
        roots.append(py_work9_cluster_root(idx,valid,items,vc))
    root,cb=py_work9_snapshot_roots(centers,books,roots)
    result=dict(list_roots=roots,root=root,codebook_root=cb,
                snapshot_build_seconds=time.perf_counter()-start)
    file.write_text(json.dumps(result,indent=2)+'\n')
    event(action='snapshot_ready',folder=str(folder),seconds=result['snapshot_build_seconds'])
    return result


def verify_binding(proof,verifier,expected_ids,roots,query,n_list,n_probe,capacity):
    try:
        public=py_work9_verify_wire(proof,verifier)
    except BaseException as exc:
        if type(exc).__name__=='PanicException':
            raise RuntimeError('Plonky2 rejected malformed compressed proof') from exc
        raise
    k=len(expected_ids)
    start=len(public)-(k+2+n_list+n_probe*capacity+len(query))
    assert start>=0
    assert public[start:start+k]==[int(x) for x in expected_ids], 'proven top-k differs'
    assert public[start+k:start+k+2]==[roots['root'],roots['codebook_root']], 'snapshot differs'
    assert public[-len(query):]==[int(x)%P for x in query], 'query differs'
    return public


def run(folder,probe,k,count,repeats,max_gates=2000000,force=False):
    proof_threads=os.environ.get('RAYON_NUM_THREADS','native')
    job=folder.name+f'-p{probe}-k{k}-q{count}-r{repeats}-t{proof_threads}'
    out=RESULTS/job;out.mkdir(parents=True,exist_ok=True)
    result_file=out/'result.json'
    if result_file.exists():return json.loads(result_file.read_text())
    meta=json.loads((folder/'model.json').read_text());cfg=meta['config']
    candidate_tuples=probe*cfg['capacity']*cfg['M']
    tuples=probe*max(cfg['capacity'],cfg['K'])*cfg['M']
    if tuples>2000000 and not force:
        result=dict(status='resource_limit',job=job,output=str(out),config=cfg,n_probe=probe,k=k,proof_threads=proof_threads,
                    candidate_lookup_tuples=candidate_tuples,relation_padded_tuples=tuples,max_allowed_lookup_tuples=2000000,
                    reason='circuit shape exceeds declared construction-memory budget before building gates; no gate/time estimate reported')
        result_file.write_text(json.dumps(result,indent=2)+'\n')
        return result
    gates=py_set_based_gate(cfg['M'],cfg['K'],cfg['D']//cfg['M'],cfg['n_list'],
                            probe,cfg['capacity'],k,True)
    if gates>max_gates and not force:
        result=dict(status='resource_limit',job=job,output=str(out),config=cfg,n_probe=probe,k=k,proof_threads=proof_threads,
                    measured_gate_count=gates,max_allowed_gates=max_gates,
                    reason='gate-only preflight exceeds declared workstation proof budget; no timing estimate')
        result_file.write_text(json.dumps(result,indent=2)+'\n')
        return result
    model=load_model(folder);meta,centers,books,codes,groups=model
    roots=snapshot(folder,model)
    data_path=ROOT/'dataset'/cfg['source']
    base=vectors(data_path/'base.fvecs',cfg['N'])
    queries=vectors(data_path/'query.fvecs')[100:100+count]
    wire=out/'wire';wire.mkdir(exist_ok=True)
    os.environ['V3DB_WIRE_DIR']=str(wire)
    path=out/'per_query.csv'
    fields=['query','repeat','recall','search_ms','query_ms','verify_ms','vo_bytes',
            'build_seconds','prove_seconds','author_verify_seconds','rss_bytes','gates',
            'proof_sha256','verified']
    prior=list(csv.DictReader(path.open())) if path.exists() else []
    done={(int(r['query']),int(r['repeat'])) for r in prior}
    allrows=prior
    vk_hash=None
    for qi,raw_query in enumerate(queries):
        if all((100+qi,rep) in done for rep in range(repeats)):continue
        # Ground truth computed outside the timed server/client sections.
        distances=np.empty(cfg['N'],dtype=np.float64)
        for lo in range(0,cfg['N'],8192):
            diff=np.asarray(base[lo:lo+8192],dtype=np.float64)-raw_query
            distances[lo:lo+8192]=np.sum(diff*diff,axis=1)
        gt=np.argpartition(distances,k-1)[:k]
        query=rescale_query(raw_query,65536,meta['vmin'],meta['vmax'])
        for rep in range(repeats):
            if (100+qi,rep) in done:continue
            start=time.perf_counter()
            dist=np.sum((centers-query)**2,axis=1)
            order=np.argsort(dist,kind='stable')
            pairs=np.column_stack((order,dist[order]))
            arrays=[list_records(groups,codes,idx,cfg['capacity'],cfg['M']) for idx in order[:probe]]
            vpq=np.stack([r[0] for r in arrays]);valid=np.stack([r[1] for r in arrays]);items=np.stack([r[2] for r in arrays])
            candidate_dist=[]
            for pos,idx in enumerate(order[:probe]):
                reconstructed=books[np.arange(cfg['M'])[None,:],vpq[pos]].reshape(cfg['capacity'],cfg['D'])
                delta=query-centers[idx]-reconstructed
                ds=np.sum(delta*delta,axis=1);ds[valid[pos]==0]=2**62-1
                candidate_dist.extend(ds.tolist())
            flat=np.asarray(candidate_dist,dtype=np.int64)
            rank=np.argsort(flat,kind='stable')[:k]
            expected=items.reshape(-1)[rank]
            assert np.all(np.concatenate(valid)[rank]==1), 'not enough valid candidates'
            search_ms=(time.perf_counter()-start)*1000
            values=py_set_based_with_merkle(query,centers,vpq,valid,items,books,
                                           roots['list_roots'],k,pairs,[])
            wall=time.perf_counter()-start
            # Author function internally verifies after compression. Remove that
            # measured internal verification from server time; include all other
            # construction/witness/prove/compression/serialization work.
            query_ms=(wall-values[2])*1000
            proof=(wire/'proof.bin').read_bytes();verifier=(wire/'verifier.bin').read_bytes()
            current=hashlib.sha256(verifier).hexdigest()
            if vk_hash is not None:assert current==vk_hash, 'circuit data changed within work point'
            vk_hash=current
            verify_start=time.perf_counter()
            verify_binding(proof,verifier,expected,roots,query,cfg['n_list'],probe,cfg['capacity'])
            verify_ms=(time.perf_counter()-verify_start)*1000
            if qi==0 and rep==0:
                (out/'query0.proof').write_bytes(proof)
                (out/'trusted_verifier.bin').write_bytes(verifier)
                tampered=bytearray(proof);tampered[0]^=1
                try:
                    verify_binding(bytes(tampered),verifier,expected,roots,query,cfg['n_list'],probe,cfg['capacity'])
                except (RuntimeError,AssertionError):
                    pass
                else:raise AssertionError('tampered serialized proof accepted')
                try:
                    verify_binding(proof,verifier,expected,roots,query+1,cfg['n_list'],probe,cfg['capacity'])
                except AssertionError:
                    pass
                else:raise AssertionError('wrong query accepted')
                (out/'tamper.json').write_text(json.dumps(dict(proof_byte_change_rejected=True,wrong_query_rejected=True)))
            recall=len(set(expected.tolist())&set(gt.tolist()))/k
            row=dict(query=100+qi,repeat=rep,recall=recall,search_ms=search_ms,
                     query_ms=query_ms,verify_ms=verify_ms,vo_bytes=len(proof),
                     build_seconds=values[0],prove_seconds=values[1],author_verify_seconds=values[2],
                     rss_bytes=values[4],gates=values[5],proof_sha256=hashlib.sha256(proof).hexdigest(),verified=1)
            isnew=not path.exists()
            with path.open('a',newline='') as f:
                writer=csv.DictWriter(f,fieldnames=fields)
                if isnew:writer.writeheader()
                writer.writerow(row)
            allrows.append(row)
            event(action='formal_sample',job=job,query=100+qi,repeat=rep,query_ms=query_ms,recall=recall)
    assert len(allrows)==count*repeats
    def mean(key):return statistics.fmean(float(r[key]) for r in allrows)
    result=dict(status='passed',job=job,output=str(out),config=cfg,n_probe=probe,k=k,
                proof_threads=proof_threads,
                query_count=count,repeats=repeats,samples=len(allrows),
                recall=mean('recall'),query_ms=mean('query_ms'),verify_ms=mean('verify_ms'),
                vo_kb=mean('vo_bytes')/1000,search_ms=mean('search_ms'),gates=gates,
                verifier_sha256=vk_hash,snapshot_root=roots['root'],codebook_root=roots['codebook_root'],
                host_alignment='indexed first-match/swap; identical to author witness output',
                author_zero_knowledge=False)
    result_file.write_text(json.dumps(result,indent=2)+'\n')
    event(action='formal_point_passed',**result)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--model',required=True)
    p.add_argument('--probe',type=int,required=True)
    p.add_argument('--k',type=int,default=10)
    p.add_argument('--queries',type=int,default=20)
    p.add_argument('--repeats',type=int,default=3)
    p.add_argument('--attempt-large',action='store_true')
    a=p.parse_args()
    print(json.dumps(run(CACHE/a.model,a.probe,a.k,a.queries,a.repeats,force=a.attempt_large),indent=2))
