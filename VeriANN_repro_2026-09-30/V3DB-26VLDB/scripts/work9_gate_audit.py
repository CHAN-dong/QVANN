import json
from work9_calibrate import STUDY,CACHE
from zk_IVF_PQ.zk_IVF_PQ import py_set_based_gate

records=[]
for probe in [1,2]:
    gates=py_set_based_gate(128,1024,1,1024,probe,2048,10,True)
    records.append(dict(dataset='sift1m',N=1000000,M=128,K=1024,n_list=1024,
                        capacity=2048,n_probe=probe,gates_before_build=gates,
                        role='gate-only resource preflight; not proof/query timing'))
(STUDY/'sift-high-precision-gates.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps(records,indent=2))
