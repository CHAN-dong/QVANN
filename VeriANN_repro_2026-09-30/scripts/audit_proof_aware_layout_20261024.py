"""Independent Python proof reconstruction, float32 replay and held-out cost audit."""
import argparse
import ast
import csv
import hashlib
import heapq
import json
import math
import random
import re
import struct
from pathlib import Path
import numpy as np
import work9_resource_matched as u

parser=argparse.ArgumentParser();parser.add_argument('--run-id',default='proof-aware-layout-20261024');args=parser.parse_args();assert re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*',args.run_id)
R=u.ROOT;S=R/'experiments'/args.run_id;O=R/'results'/args.run_id
assert u.read(S/'FINISHED.json')['passed']
# Reuse only pure Python audit definitions, never run the prior experiment or
# its native code. The 4-byte SQ4 tag adapter below is outside the audited VO.
helper=R/'scripts/audit_routing_precision_20261024.py'
tree=ast.parse(helper.read_text())
defs=[x for x in tree.body if isinstance(x,(ast.FunctionDef,ast.ClassDef))]
exec(compile(ast.Module(body=defs,type_ignores=[]),str(helper),'exec'),globals())

def sibling_count(positions,n):
    target=set(map(int,positions));count=0
    for _ in range((n-1).bit_length()):
        count+=sum((x^1) not in target for x in target)
        target={x//2 for x in target}
    return count

protocol=u.read(S/'protocol.json');build=u.read(S/'build.json')
assert u.sha(R/'src/experiments/proof_aware_layout.cpp')==build['source_sha256']
assert u.sha(R/'build/veriann_lut_benchmark.cpp')==build['original_sha256']
assert u.sha(S/'layout_original_include.cpp')==build['include_sha256']
assert u.sha(S/'layout_packet_generator.cpp')==build['serializer_sha256']
assert u.sha(S/'proof_aware_layout')==build['binary_sha256']
summary=u.read(S/'summary.json');checks=wire_checks=negative=0;examples=[]
for ds,cfg in protocol['datasets'].items():
    out=O/ds;split=u.read(S/ds/'split.json');ids=list(range(split['query_count']));random.Random(protocol['seed']).shuffle(ids)
    assert split['train']==ids[:256] and split['test']==ids[256:356]
    assert not set(split['train'])&set(split['test']) and u.sha(split['query_file'])==split['query_file_sha256']
    assert u.sha(cfg['graph'])==cfg['graph_sha256']
    anchors={r['layout']:r for r in csv.DictReader((out/'anchors.csv').open())}
    n=int(anchors['ID']['route_count']);d=int(anchors['ID']['D'])
    positions={name:np.fromfile(out/(name+'.positions.u32'),dtype='<u4') for name in protocol['layouts']}
    for name,v in positions.items():assert len(v)==n and np.array_equal(np.sort(v),np.arange(n,dtype=np.uint32))
    assert np.array_equal(positions['ID'],np.arange(n,dtype=np.uint32))
    traces={(r['split'],int(r['query'])):np.asarray(list(map(int,r['records'].split())),dtype=np.uint32) for r in csv.DictReader((out/'traces.csv').open())}
    assert {q for part,q in traces if part=='train'}==set(split['train'])
    assert {q for part,q in traces if part=='test'}==set(split['test'])
    costs=list(csv.DictReader((out/'proof_costs.csv').open()));assert len(costs)==356*4
    for r in costs:
        records=traces[r['split'],int(r['query'])];assert len(records)==len(set(records))==int(r['targets'])
        assert sibling_count(positions[r['layout']][records],n)==int(r['sibling_hashes']);checks+=1
    history=list(csv.DictReader((out/'optimization.csv').open()));assert len(history)==5
    for prev,cur in zip(history,history[1:]):assert prev['after_hashes']==cur['before_hashes'] and int(cur['after_hashes'])<=int(cur['before_hashes'])
    for name,key,row in [('BFS','before_hashes',history[0]),('Proof-Aware','after_hashes',history[-1])]:
        assert sum(int(r['sibling_hashes']) for r in costs if r['layout']==name and r['split']=='train')==int(row[key])
    raw=list(csv.DictReader((out/'samples.csv').open()));assert len(raw)==1200
    truth={int(r['query']):set(map(int,r['answer'].split())) for r in csv.DictReader((out/'ground_truth.csv').open())}
    cc={(r['layout'],int(r['query'])):r for r in costs if r['split']=='test'}
    for r in raw:
        q=int(r['query']);assert r['valid']=='1' and len(set(map(int,r['answer'].split()))&truth[q])==int(r['hits'])
        assert int(r['sibling_hashes'])==int(cc[r['layout'],q]['sibling_hashes'])
        assert int(r['route_proof_bytes'])==4+32*int(r['sibling_hashes'])
        assert int(r['payload_bytes'])+int(r['auth_proof_bytes'])==int(r['total_bytes'])
    base=fvecs(R/'dataset'/ds/'sift_base.fvecs',d);queries=fvecs(Path(split['query_file']),d)
    lo=np.asarray(base).min(axis=0);span=np.asarray(base).max(axis=0)-lo
    for name in protocol['layouts']:
        anchor={k:int(v) if k in ['N','D','entry','maxlevel','route_count'] else v for k,v in anchors[name].items()}
        assert anchor['snapshot']==cfg['manifest']['dataset_sha256'];anchor.update(lo=lo.tolist(),span=span.tolist())
        for qi in split['test'][:2]:
            path=out/f'sample-{qi}-{name}.vo';adapter=S/'audit-native-wire-adapter.vo';adapter.write_bytes(U(1)+path.read_bytes())
            r=independent_verify(adapter,anchor,np.asarray(queries[qi]),cfg['E'],False,base)
            assert r['valid'],(ds,name,qi,r)
            row=next(x for x in raw if x['layout']==name and int(x['query'])==qi and x['repeat']=='0')
            assert r['vo_bytes']==int(row['total_bytes'])+4 and r['proof_bytes']==int(row['auth_proof_bytes'])
            assert r['answer']==list(map(int,row['answer'].split())) and r['opened']==int(row['targets'])
            # Reconstruct the current experiment's sorted opened/candidate trace
            # fingerprint from independently decoded packet records.
            wire=path.read_bytes();reader=Reader(wire);reader.blob(88)
            candidates=[]
            for _ in range(reader.u32()):candidates.append(reader.u32());reader.blob(d//2)
            opened=[]
            for _ in range(reader.u32()):
                id,level,pos=reader.u32(),reader.u32(),reader.u32();opened.append((id,level));reader.blob(reader.u32()*4)
            fingerprint=b''.join(U(id)+U(level) for id,level in opened)+b''.join(U(id) for id in candidates)
            assert h(fingerprint).hex()==row['trace_sha256']
            wire_checks+=1;examples.append(dict(dataset=ds,layout=name,query=qi,independent_authentication_and_replay=True))
        tamper=list(csv.DictReader((out/f'tamper-{name}/tamper_tests.csv').open()));assert len(tamper)==13 and all(r['rejected']=='1' for r in tamper)
        assert (out/f'tamper-{name}/position_tamper.txt').read_text().strip()=='Rejected';negative+=14
    for x in [r for r in summary if r['dataset']==ds]:
        rr=[r for r in raw if r['layout']==x['Layout']]
        for target,source,scale in [('Total_VO_KiB','total_bytes',1024),('Routing_Proof_KiB','route_proof_bytes',1024),('Sibling_Hashes','sibling_hashes',1),('V_ms','verify_ms',1)]:
            assert math.isclose(x[target],sum(float(r[source]) for r in rr)/len(rr)/scale,rel_tol=1e-12)
adapter.unlink()
report=dict(passed=True,independent_true_cost_counts=checks,independent_full_wire_Merkle_float32_replay_and_results=wire_checks,native_negative_tests=negative,native_formal_verified_VOs=2400,held_out_split_verified=True,permutations_verified=True,optimization_objective_verified=True,all_graph_hashes_preserved=True,helper_sha256=u.sha(helper),examples=examples)
u.write(S/'independent-audit.json',report)
print(json.dumps({k:v for k,v in report.items() if k!='examples'},indent=2))
