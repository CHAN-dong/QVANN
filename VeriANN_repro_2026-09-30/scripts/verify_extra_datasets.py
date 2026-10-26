"""Verify prepared ANN vectors, fingerprints, normalization and loader aliases."""
import json
import os
import numpy as np
from download_extra_datasets import ROOT, sha

def main():
    results=[]
    for name in ['gist1m','glove100','deep1m']:
        folder=ROOT/'dataset'/name
        p=json.loads((folder/'provenance.json').read_text());assert p['status']=='PASS'
        for key,entry in p['files'].items():
            path=folder/entry['file'];n,d=entry['shape']
            assert path.stat().st_size==entry['bytes']==n*(d+1)*4
            assert sha(path)==entry['sha256']
            dtype='<i4' if key=='neighbors' else '<f4'
            data=np.memmap(path,dtype=dtype,mode='r',shape=(n,d+1))
            assert np.all(data.view('<i4')[:,0]==d)
            for start in range(0,n,8192):
                a=data[start:start+8192,1:]
                assert np.all(np.isfinite(a))
                if key=='neighbors':assert np.all((a>=0)&(a<p['files']['train']['shape'][0]))
                elif p['normalized']:
                    norms=np.sqrt(np.sum(a.astype(np.float64)**2,axis=1))
                    assert np.max(np.abs(norms-1))<1e-6
        for canonical,legacy in [('base.fvecs','sift_base.fvecs'),('query.fvecs','sift_query.fvecs')]:assert os.path.samefile(folder/canonical,folder/legacy)
        for meta in (p['sources'].values() if 'sources' in p else [p['source']]):
            matches=[raw for raw in (folder/'raw').glob('*.download.json') if json.loads(raw.read_text())['url']==meta['url']]
            assert len(matches)==1
            original=matches[0].with_name(matches[0].name.removesuffix('.download.json'))
            assert sha(original)==meta['sha256']
        result=dict(dataset=name,status='PASS',base=p['files']['train']['shape'][0],dimension=p['files']['train']['shape'][1],queries=p['files']['test']['shape'][0],metric=p['metric'])
        results.append(result);print(json.dumps(result),flush=True)
    (ROOT/'dataset/extra_datasets_final_validation.json').write_text(json.dumps(dict(status='PASS',datasets=results),indent=2)+'\n')

if __name__=='__main__':main()
