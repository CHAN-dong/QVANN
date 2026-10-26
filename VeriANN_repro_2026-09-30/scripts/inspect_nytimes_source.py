from pathlib import Path
import json
import h5py
import numpy as np
root=Path(__file__).resolve().parents[1]
path=root/'dataset/nytimes256/raw/nytimes-256-angular.hdf5'
record={}
with h5py.File(path,'r') as h:
    for key in ['train','test']:
        zeros=[]
        for start in range(0,len(h[key]),8192):
            a=h[key][start:start+8192]
            zeros.extend((start+np.flatnonzero(np.sum(a.astype(np.float64)**2,axis=1)==0)).tolist())
        record[key]=dict(shape=list(h[key].shape),zero_rows=zeros)
    record['selected_query_zero_rows']=[q for q in record['test']['zero_rows'] if 100<=q<120]
print(json.dumps(record))
(root/'experiments/work9-nytimes-20261017/source_zero_audit.json').write_text(json.dumps(record,indent=2)+'\n')
