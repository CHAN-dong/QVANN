"""Convert the ANN-Benchmarks SIFT1M mirror to canonical fvecs, in chunks."""
from pathlib import Path
import hashlib
import json
import h5py
import numpy as np

root = Path(__file__).resolve().parents[1]
folder = root / 'dataset/sift1m'
source = folder / 'sift-128-euclidean.hdf5'
def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        while block := f.read(4*1024*1024):
            h.update(block)
    return h.hexdigest()

metadata = {'url':'https://ann-benchmarks.com/sift-128-euclidean.hdf5',
            'source_sha256':sha256(source)}
with h5py.File(source,'r') as f:
    for key,name,kind in [('train','sift_base.fvecs','<f4'),('test','sift_query.fvecs','<f4'),
                          ('neighbors','sift_groundtruth.ivecs','<i4')]:
        dataset = f[key]
        n,d = dataset.shape
        if key=='train' and (n,d)!=(1000000,128):
            raise SystemExit('unexpected SIFT1M shape')
        target = folder / name
        with target.open('wb') as output:
            for begin in range(0,n,8192):
                rows = np.empty((min(8192,n-begin),d+1),dtype=kind)
                rows[:,1:] = dataset[begin:begin+8192]
                if kind=='<f4':
                    rows.view('<i4')[:,0] = d
                else:
                    rows[:,0] = d
                output.write(rows.tobytes())
        metadata[key]={'shape':[n,d],'sha256':sha256(target),'bytes':target.stat().st_size}
    # Verify whether this mirror contains the exact 100K prefix used previously.
    for name in ('sift_base.fvecs','sift_query.fvecs'):
        old=root/'dataset/sift'/name
        with (folder/name).open('rb') as new:
            metadata[name+'_matches_packaged_prefix']=hashlib.sha256(new.read(old.stat().st_size)).hexdigest()==sha256(old)
(folder/'provenance.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(json.dumps(metadata,indent=2))
