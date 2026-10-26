"""Read-only byte accounting of actual compressed proofs, including public inputs."""
import json
import os
from pathlib import Path
os.environ.setdefault('RAYON_NUM_THREADS','1')
from zk_IVF_PQ.zk_IVF_PQ import py_work9_verify_wire

root=Path(__file__).resolve().parents[2]
reports=[]
for name in ['sift1m-1000000-M128-K1024-p2-k10-q20-r3-t1',
             'nytimes256-289761-M256-p2-k10-q20-r3-t1']:
    folder=root/'results/work9-v3db-20261016'/name
    result=json.loads((folder/'result.json').read_text())
    proof=(folder/'query0.proof').read_bytes()
    public=py_work9_verify_wire(proof,(folder/'trusted_verifier.bin').read_bytes())
    c=result['config'];prefix=max(c['capacity'],c['M'],c['K'],result['n_probe'])
    expected=prefix+1+result['k']+2+c['n_list']+result['n_probe']*c['capacity']+c['D']
    assert len(public)==expected,(name,len(public),expected,public[prefix-3:prefix+8])
    assert public[:prefix]==list(range(prefix))
    assert public[prefix]==0
    start=prefix+1+result['k']+2
    assert public[start:start+c['n_list']]==list(range(c['n_list']))
    start+=c['n_list']
    assert public[start:start+result['n_probe']*c['capacity']]==list(range(c['capacity']))*result['n_probe']
    # Length is pretrusted in CommonCircuitData, not written before write_field_vec.
    assert proof[-8*len(public):]==b''.join(int(x).to_bytes(8,'little') for x in public)
    r=dict(job=name,sample='query100 repeat0',actual_wire_bytes=len(proof),
        public_field_count=len(public),public_field_bytes=8*len(public),public_length_bytes=0,
        cryptographic_compressed_proof_bytes=len(proof)-8*len(public),
        public_constant_lookup_numbers=prefix,public_constant_zero=1,
        public_constant_centroid_numbers=c['n_list'],
        public_constant_cluster_slot_numbers=result['n_probe']*c['capacity'],
        deterministic_constant_bytes=(prefix+1+c['n_list']+result['n_probe']*c['capacity'])*8,
        public_query_dimensions=c['D'],public_topk=result['k'],public_snapshot_roots=2,
        mean_wire_kb_over_60=result['vo_kb'],actual_measured_recall=result['recall'],gates=result['gates'])
    reports.append(r)
out=root/'experiments/work9-v3db-20261016/vo-breakdown.json'
out.write_text(json.dumps(reports,indent=2)+'\n')
print(json.dumps(reports,indent=2))
