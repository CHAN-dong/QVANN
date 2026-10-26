import json
import os
from pathlib import Path
import sys
from work9_calibrate import REPRO, STUDY, bounded_rebalance,score_all_queries,train_pq_subspaces
import numpy as np
from ivf_pq.rebalance import rebalance_clusters
from bench import data_gen, MAX_DIS
from zk_IVF_PQ.zk_IVF_PQ import (py_set_based_with_merkle,py_work9_verify_wire,
    py_work9_snapshot_roots,py_work9_cluster_root,py_work9_align_sets,py_set_based_gate)

rng=np.random.default_rng(42)
def reference_alignment(frows,trows,alpha):
    prime=2**64-2**32+1
    def compress(row):
        value=int(row[0])%prime
        for x in row[1:]:value=(value*alpha+int(x))%prime
        return value
    f=list(map(compress,frows));t=list(map(compress,trows));size=max(len(f),len(t))
    f+= [f[0]]*(size-len(f));t+=[t[0]]*(size-len(t));f.sort()
    for i in range(size):
        if t[i]==f[0]:t[i],t[0]=t[0],f[0]
    t[1:]=sorted(t[1:])
    for i in range(1,size):
        if f[i]!=f[i-1]:
            for j in range(size):
                if t[j]==f[i]:t[j],t[i]=t[i],f[i];break
    return f,t
for case in range(256):
    trows=rng.integers(-8,9,(int(rng.integers(1,100)),4)).tolist()
    frows=[trows[int(i)] for i in rng.integers(0,len(trows),int(rng.integers(1,150)))]
    alpha=[0,1,2**64-2**32,int(rng.integers(2,1000000))][case%4]
    expected=reference_alignment(frows,trows,alpha)
    actual=py_work9_align_sets(frows,trows,alpha)
    assert actual==expected,(case,'alignment output differs')
assert py_set_based_gate(8,16,16,256,16,32,64,True)==21198
testq=rng.integers(0,65536,(20,16),dtype=np.int64)
testcenters=rng.integers(0,65536,(8,16),dtype=np.int64)
testbooks=rng.integers(-65536,65536,(4,16,4),dtype=np.int64)
testcodes=rng.integers(0,16,(256,4),dtype=np.int64)
testlabels=rng.integers(0,8,256,dtype=np.int64)
batched=score_all_queries(testq,testcenters,testbooks,testcodes,testlabels)
reconstructed=testbooks[np.arange(4)[None,:],testcodes].reshape(256,16)
direct=np.stack([np.sum((q-testcenters[testlabels]-reconstructed)**2,axis=1) for q in testq])
assert np.array_equal(batched,direct)
import faiss
from ivf_pq.util.kmeans import faiss_kmeans_with_ids
faiss.omp_set_num_threads(1)
training=rng.integers(0,65536,(256,16),dtype=np.int64)
serial_books=[];serial_codes=[]
for lo in range(0,16,4):
    book,_,code=faiss_kmeans_with_ids(training[:,lo:lo+4]-testcenters[testlabels,lo:lo+4],16,niter=3,random_state=0)
    serial_books.append(book);serial_codes.append(code)
parallel_books,parallel_codes=train_pq_subspaces(training,testcenters,testlabels,4,16,workers=2,niter=3)
assert np.array_equal(parallel_books,np.asarray(serial_books))
assert np.array_equal(parallel_codes,np.asarray(serial_codes).T)
x=rng.integers(0,100,(256,16));centers=rng.integers(0,100,(16,16))
labels=np.zeros(256,dtype=np.int64)
orig=rebalance_clusters(x,centers,labels,32)
bounded_rebalance()
import ivf_pq.zk
chunked=ivf_pq.zk.rebalance_clusters(x,centers,labels,32)
assert np.array_equal(orig[0],chunked[0]) and orig[2]==chunked[2]

q,centers,sel,codes,valid,items,books,roots=data_gen(16,8,2,4,8,2,32)
for pos,idx in enumerate(sel):
    assert py_work9_cluster_root(int(idx),valid[pos],items[pos],codes[pos])==roots[idx]
root,cb=py_work9_snapshot_roots(centers,books,roots)
dist=np.sum((centers-q)**2,axis=1);order=np.argsort(dist,kind='stable')
pairs=np.column_stack((order,dist[order]))
flat=[]
for pos,idx in enumerate(sel):
    delta=q-centers[idx]-books[np.arange(2)[None,:],codes[pos]].reshape(32,16)
    ds=np.sum(delta*delta,axis=1);ds[valid[pos]==0]=MAX_DIS;flat.extend(ds.tolist())
top=items.reshape(-1)[np.argsort(flat,kind='stable')[:4]].tolist()
out=STUDY/'adapter-smoke';out.mkdir(exist_ok=True)
os.environ['V3DB_WIRE_DIR']=str(out)
values=py_set_based_with_merkle(q,centers,codes,valid,items,books,roots,4,pairs,[])
proof=(out/'proof.bin').read_bytes();verifier=(out/'verifier.bin').read_bytes()
public=py_work9_verify_wire(proof,verifier)
start=len(public)-(4+2+8+2*32+16)
assert public[start:start+4]==top and public[start+4:start+6]==[root,cb] and public[-16:]==q.tolist()
bad=bytearray(proof);bad[0]^=1
try:py_work9_verify_wire(bytes(bad),verifier)
except BaseException as exc:
    if not isinstance(exc,RuntimeError) and type(exc).__name__!='PanicException':raise
else:raise AssertionError('tampered proof accepted')
result=dict(chunked_rebalance_identical=True,cluster_roots_identical=True,
            indexed_alignment_matches_author_256_cases=True,paper_basic_gate_count_unchanged=True,
            batched_integer_pq_scores_identical=True,
            parallel_pq_matches_serial_author_calls=True,
            serialized_proof_verified=True,public_topk_query_roots_match=True,
            tampered_wire_proof_rejected=True,gates=values[5],vo_bytes=len(proof),
            note='adapter functionality test; concurrent calibration means timings excluded')
(STUDY/'adapter-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
