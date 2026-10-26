"""Independent Python wire/Merkle/float32 routing audit; no native verifier calls."""
import csv
import argparse
import hashlib
import heapq
import json
import math
import re
import struct
from collections import defaultdict
from pathlib import Path
import numpy as np
import work9_resource_matched as u

parser=argparse.ArgumentParser();parser.add_argument('--run-id',default='routing-precision-20261024');args=parser.parse_args();assert re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*',args.run_id)
R=u.ROOT;S=R/'experiments'/args.run_id;O=R/'results'/args.run_id
assert u.read(S/'FINISHED.json')['passed']
protocol=u.read(S/'protocol.json');chosen=u.read(S/'calibration.json');summary=u.read(S/'summary.json')
def h(data):return hashlib.sha256(data).digest()
def U(x):return struct.pack('<I',int(x))
class Reader:
    def __init__(self,b):self.b=b;self.pos=0
    def blob(self,n):assert 0<=n<=len(self.b)-self.pos;v=self.b[self.pos:self.pos+n];self.pos+=n;return v
    def u32(self):return struct.unpack('<I',self.blob(4))[0]
    def proof(self,root,n,leaves):
        count=self.u32();proof=[self.blob(32) for _ in range(count)]
        if not leaves:assert count==0;return
        assert all(0<=x<n for x in leaves);width=1<<(n-1).bit_length();current=leaves;index=0
        while width>1:
            nxt={}
            for node in sorted(current):
                if node^1 in current:sibling=current[node^1]
                else:assert index<count;sibling=proof[index];index+=1
                digest=h(sibling+current[node]) if node&1 else h(current[node]+sibling)
                if node//2 in nxt:assert nxt[node//2]==digest
                nxt[node//2]=digest
            current=nxt;width//=2
        assert index==count and current=={0:bytes.fromhex(root)}

def fvecs(path,d):return np.memmap(path,dtype='<f4',mode='r').reshape(-1,d+1)[:,1:]
def quant4(v,lo,span):
    value=np.divide(v-lo,span,out=np.zeros_like(v),where=span>0)
    return (np.clip(np.floor(value*np.float32(64)),0,63).astype(np.uint8)>>2)
def distances(v,q):
    total=np.zeros(len(v),dtype=np.float32)
    for j in range(v.shape[1]):
        z=q[j]-v[:,j];total=total+z*z
    return total
def scores4(codes,q,lo,span):
    total=np.zeros(len(codes),dtype=np.float32)
    for j in range(len(q)):
        x=lo[j]+span[j]*((codes[:,j].astype(np.float32)+np.float32(.5))/np.float32(16));z=q[j]-x;total=total+z*z
    return total
def lower(codes,q,lo,span,bits):
    total=np.zeros(len(codes),dtype=np.float32);bins=np.float32(1<<bits)
    for j in range(len(q)):
        a=np.nextafter(lo[j]+span[j]*(codes[:,j].astype(np.float32)/bins),np.float32(-np.inf))
        b=np.nextafter(lo[j]+span[j]*((codes[:,j].astype(np.float32)+np.float32(1))/bins),np.float32(np.inf))
        z=np.where(q[j]<a,a-q[j],np.where(q[j]>b,q[j]-b,np.float32(0)));total=total+z*z
    return np.nextafter(total,np.float32(0))
def unpack4(b,d):
    return np.array([(b[j//2]>>((j%2)*4))&15 for j in range(d)],dtype=np.uint8)

def replay(score,links,entry,maxlevel,E):
    seen=set();expanded=set();opened=set();order=[]
    def discover(id):assert id in score;seen.add(id)
    def open_node(id,level):
        key=id,level
        if key in opened:return []
        if id not in expanded:
            if len(expanded)>=E:return []
            expanded.add(id)
        opened.add(key);order.append(key);return links[key]
    discover(entry);stopped=False
    for level in range(maxlevel,0,-1):
        if stopped:break
        while True:
            neighbors=open_node(entry,level)
            if not neighbors:stopped=len(expanded)>=E;break
            for id in neighbors:discover(id)
            nxt=min(neighbors+[entry],key=lambda id:(score[id],id))
            if nxt==entry:break
            entry=nxt
    frontier=[(score[id],id) for id in seen];heapq.heapify(frontier)
    while len(expanded)<E and frontier:
        _,id=heapq.heappop(frontier)
        for nb in open_node(id,0):
            fresh=nb not in seen;discover(nb)
            if fresh:heapq.heappush(frontier,(score[nb],nb))
    return seen,opened,order,expanded

def independent_verify(path,anchor,q,E,exact,base):
    authenticated=False
    try:
        r=Reader(path.read_bytes());assert r.u32()==(0 if exact else 1);assert r.u32()==0x56505231;assert r.blob(32)==h(q.astype('<f4').tobytes());assert r.u32()==10 and r.u32()==E and r.u32()==10;answer=[r.u32() for _ in range(10)]
        n,d=anchor['N'],anchor['D'];lo=np.asarray(anchor['lo'],np.float32);span=np.asarray(anchor['span'],np.float32)
        codes={}
        for _ in range(r.u32()):
            id=r.u32();assert id<n and id not in codes;codes[id]=r.blob(d//2)
        ids=sorted(codes);c4=np.stack([unpack4(codes[id],d) for id in ids]);code_index={id:i for i,id in enumerate(ids)}
        assert np.array_equal(c4[code_index[anchor['entry']]],quant4(np.asarray(base[anchor['entry']]),lo,span))
        links={};route_leaves={}
        for _ in range(r.u32()):
            id,l,pos=r.u32(),r.u32(),r.u32();assert id<n and l<=anchor['maxlevel'] and pos<anchor['route_count']
            ns=[r.u32() for _ in range(r.u32())];assert ns==sorted(set(ns)) and (id,l) not in links and pos not in route_leaves
            links[id,l]=ns
            leaf=b'record/expansion-direct/v1'+bytes.fromhex(anchor['snapshot'])+U(id)+U(l)+U(len(ns))
            for nb in ns:leaf+=U(nb)+codes[nb]
            route_leaves[pos]=h(leaf)
        delta={};delta_leaves={}
        for _ in range(r.u32()):
            id,pos=r.u32(),r.u32();b=r.blob(d//4);assert id in codes and id not in delta and pos not in delta_leaves
            delta[id]=b;delta_leaves[pos]=h(b'record/delta46/v1'+U(id)+b)
        raw={};raw_leaves={}
        for _ in range(r.u32()):
            id,pos=r.u32(),r.u32();b=r.blob(d*4);assert id in codes and id not in raw and pos not in raw_leaves
            assert exact or id in delta
            raw[id]=np.frombuffer(b,dtype='<f4').copy();assert np.isfinite(raw[id]).all()
            raw_leaves[pos]=h(b'record/raw/v1'+U(id)+b)
        proof_start=r.pos;r.proof(anchor['route'],anchor['route_count'],route_leaves);r.proof(anchor['delta'],n,delta_leaves);r.proof(anchor['raw'],n,raw_leaves);assert r.pos==len(r.b);authenticated=True
        if exact:assert set(raw)==set(codes);vv=np.stack([raw[id] for id in ids]);score_values=distances(vv,q)
        else:score_values=scores4(c4,q,lo,span)
        scores={id:float(value) for id,value in zip(ids,score_values)}
        seen,opened,order,expanded=replay(scores,links,anchor['entry'],anchor['maxlevel'],E)
        assert seen==set(codes) and opened==set(links)
        raw_ids=sorted(raw);raw_dist=distances(np.stack([raw[id] for id in raw_ids]),q)
        best=sorted(zip(map(float,raw_dist),raw_ids));assert [id for _,id in best[:10]]==answer and len(set(answer))==10;tau=np.float32(best[9][0])
        l4=lower(c4,q,lo,span,4);expected_delta={id for id,x in zip(ids,l4) if x<=tau};assert set(delta)==expected_delta
        refined=sorted(delta);code6=[]
        for id in refined:
            b=delta[id];code6.append((c4[code_index[id]]<<2)|np.array([(b[j//4]>>((j%4)*2))&3 for j in range(d)],np.uint8))
        l6=lower(np.stack(code6),q,lo,span,6);expected_raw={id for id,x in zip(refined,l6) if x<=tau or id in answer}
        assert set(raw)==(set(codes) if exact else expected_raw)
        transcript=b''.join(U(id) for id in sorted(seen))+b''.join(U(id)+U(level) for id,level in order)
        return dict(valid=True,auth_passed=True,trace_sha256=h(transcript).hex(),candidates=len(seen),expanded=len(expanded),opened=len(opened),raw_records=len(raw),final_raw_records=len(expected_raw),proof_bytes=len(r.b)-proof_start,vo_bytes=len(r.b),answer=answer)
    except (AssertionError,KeyError,IndexError,ValueError,struct.error) as error:
        return dict(valid=False,auth_passed=authenticated,error=type(error).__name__+': '+str(error))

calibration_rows=formal_rows=step_checks=positive_wires=semantic_wires=legacy=negative=0;audited=[]
for ds,cfg in protocol['datasets'].items():
    assert u.sha(cfg['graph'])==cfg['graph_sha256']
    pilot=list(csv.DictReader((O/ds/'pilot/calibration.csv').open()));rows=list(csv.DictReader((O/ds/'formal/samples.csv').open()))
    anchor=u.read(O/ds/'formal/anchor.json');d=anchor['D'];base=fvecs(R/'dataset'/ds/'sift_base.fvecs',d);queries=fvecs(R/'dataset'/ds/'sift_query.fvecs',d)
    truth={int(r['query']):set(map(int,r['answer'].split())) for r in csv.DictReader((O/ds/'formal/ground_truth.csv').open())}
    points=defaultdict(list)
    for row in pilot:
        a=set(map(int,row['answer'].split()));hits=len(a&truth[int(row['query'])]);assert hits==int(row['hits']) and math.isclose(hits/10,float(row['recall']),abs_tol=1e-12)
        points[row['routing'],int(row['E'])].append(hits);calibration_rows+=1
    for mode in ['Exact','SQ4']:
        recalls={E:sum(points[mode,E])/200 for E in range(1,257)}
        assert chosen[ds][mode]['E']==min(E for E,value in recalls.items() if value==.95),'not the smallest exact95% point'
    for row in rows:
        hits=len(set(map(int,row['answer'].split()))&truth[int(row['query'])]);assert hits==int(row['hits']) and row['valid']=='1'
        assert row['trace_sha256']==row['client_trace_sha256']
        assert int(row['raw_records'])==int(row['final_raw_records'])+int(row['routing_only_raw_records'])
        if row['routing']=='Exact':assert row['raw_records']==row['candidates']
        else:assert int(row['routing_only_raw_records'])==0
        formal_rows+=1
    step_groups=defaultdict(list)
    for row in csv.DictReader((O/ds/'formal/expansions.csv').open()):step_groups[row['setting'],row['routing'],int(row['E']),int(row['query']),int(row['repeat'])].append(row)
    lo=np.asarray(anchor['lo'],np.float32);span=np.asarray(anchor['span'],np.float32)
    for row in rows:
        key=row['setting'],row['routing'],int(row['E']),int(row['query']),int(row['repeat']);ss=step_groups[key]
        assert len(ss)==int(row['opened']) and [int(s['step']) for s in ss]==list(range(len(ss)))
        nodes=[int(s['node']) for s in ss];assert len(set(nodes))==int(row['expanded'])
        vectors=np.asarray(base[nodes]);q=np.asarray(queries[key[3]])
        expected=distances(vectors,q) if key[1]=='Exact' else scores4(quant4(vectors,lo,span),q,lo,span)
        logged=np.asarray([float(s['distance']) for s in ss],np.float32)
        assert np.array_equal(expected,logged),(ds,key,'routing score not actual requested precision');step_checks+=len(ss)
    for setting,setting_index in [('Fixed E',0),('Matched R',1)]:
        for mode in ['Exact','SQ4']:
            rec=next(r for r in summary if r['dataset']==ds and r['Setting']==setting and r['Routing']==mode)
            rr=[r for r in rows if r['setting']==setting and r['routing']==mode]
            assert len(rr)==60
            for metric in ['recall','candidates','vo_bytes','verify_ms']:
                assert math.isclose(rec[metric],sum(float(r[metric]) for r in rr)/60,rel_tol=1e-12)
            path=O/ds/'formal'/f'{setting_index}-{mode}.vo';q=np.asarray(queries[100]);out=independent_verify(path,anchor,q,rec['E'],mode=='Exact',base)
            assert out['valid'],(ds,setting,mode,out)
            first=next(r for r in rr if r['query']=='100' and r['repeat']=='0')
            for metric in ['trace_sha256','candidates','expanded','opened','raw_records','final_raw_records','proof_bytes','vo_bytes']:
                assert str(out[metric])==first[metric],(ds,setting,mode,metric,out[metric],first[metric])
            assert out['answer']==list(map(int,first['answer'].split()));positive_wires+=1;audited.append(dict(dataset=ds,setting=setting,routing=mode,file=str(path),**out))
        # Native witness can use a query after100; determine its bound query hash.
        path=O/ds/'formal'/f'semantic-witness-{setting_index}.vo';data=path.read_bytes();query_hash=data[8:40]
        qid=next(i for i in range(100,120) if h(np.asarray(queries[i]).astype('<f4').tobytes())==query_hash)
        E=cfg['E'] if setting_index==0 else chosen[ds]['Exact']['E']
        out=independent_verify(path,anchor,np.asarray(queries[qid]),E,True,base)
        assert out['auth_passed'] and not out['valid'],(ds,setting,'semantic false positive',out);semantic_wires+=1
    tests=list(csv.DictReader((O/ds/'formal/tests.csv').open()));assert len(tests)==58 and all(r['rejected']=='1' for r in tests);negative+=len(tests)
    identity=u.read(O/ds/'formal/identity.json');assert identity['legacy_checks']==120;legacy+=identity['legacy_checks']
build=u.read(S/'build.json');assert u.sha(R/'src/experiments/routing_precision.cpp')==build['source_sha256'] and u.sha(R/'build/veriann_lut_benchmark.cpp')==build['original_sha256']
assert (calibration_rows,formal_rows,positive_wires,semantic_wires,legacy,negative)==(20480,480,8,4,240,116)
report=dict(passed=True,independently_recounted_calibration_rows=calibration_rows,formal_rows_checked=formal_rows,expanded_step_scores_recomputed_from_original_vectors=step_checks,independent_full_VO_and_Merkle_and_routing_checks=positive_wires,independent_auth_passing_semantic_rejections=semantic_wires,native_legacy_equivalence_checks=legacy,native_negative_cases=negative,matched_recall_exact95_each=True,all_original_graph_hashes_preserved=True,audited_examples=audited)
u.write(S/'independent-audit.json',report)
print(json.dumps({k:v for k,v in report.items() if k!='audited_examples'},indent=2))
