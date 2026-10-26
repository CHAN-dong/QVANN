"""Check blockwise preprocessing against the author's scalar expression."""
import json
import numpy as np
from work9_calibrate import ROOT,STUDY,CACHE,DATA,vectors

results=[]
for key,(_,n,d,_,_) in DATA.items():
    folder=CACHE/f'{key}-{n}'
    meta=json.loads((folder/'model.json').read_text())
    raw=vectors(ROOT/'dataset'/key/'base.fvecs',n)
    scaled=np.load(folder/'scaled.npy',mmap_mode='r')
    scale=65535/(meta['vmax']-meta['vmin'])
    mismatch=0
    # Check every coordinate, not a sampled subset.
    for lo in range(0,n,8192):
        expected=np.clip(np.rint((np.asarray(raw[lo:lo+8192],dtype=np.float64)-meta['vmin'])*scale),0,65535).astype(np.int64)
        mismatch+=int(np.count_nonzero(expected!=scaled[lo:lo+8192]))
    results.append(dict(dataset=key,coordinates=n*d,mismatches=mismatch))
(STUDY/'scaling-validation.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
assert not any(x['mismatches'] for x in results)
