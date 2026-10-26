"""Prepare and validate GIST1M, GloVe100 and official DEEP1B's 1M prefix."""
import hashlib
import json
import os
from pathlib import Path
import struct
import h5py
import numpy as np
from download_extra_datasets import ROOT, sha

CHUNK=8192

def normalize(a):
    a=np.asarray(a,dtype='<f4').copy()
    norms=np.sqrt(np.sum(a.astype(np.float64)**2,axis=1))
    assert np.all(norms>0),'zero vector: angular preprocessing undefined'
    a*=np.asarray(1/norms,dtype='<f4')[:,None]
    return a

def write_vectors(source,target,kind='<f4',normalized=False):
    n,d=source.shape
    tmp=target.with_suffix(target.suffix+'.part')
    with tmp.open('wb') as f:
        for start in range(0,n,CHUNK):
            a=np.asarray(source[start:start+CHUNK],dtype=kind)
            if normalized:a=normalize(a)
            assert np.all(np.isfinite(a))
            out=np.empty((len(a),d+1),dtype=kind)
            out.view('<i4')[:,0]=d;out[:,1:]=a
            f.write(out.tobytes())
    assert tmp.stat().st_size==n*(d+1)*4
    tmp.replace(target)
    # Verify every row header and several values directly against the source.
    rows=np.memmap(target,dtype=kind,mode='r',shape=(n,d+1))
    assert np.all(rows.view('<i4')[:,0]==d)
    samples=np.unique(np.linspace(0,n-1,min(31,n),dtype=int))
    for i in samples:
        original=np.asarray(source[i:i+1],dtype=kind)
        if normalized:original=normalize(original)
        assert np.array_equal(rows[i,1:],original[0]),(target,i)
    return dict(file=target.name,shape=[n,d],bytes=target.stat().st_size,sha256=sha(target),
                all_row_headers_verified=True,source_sample_rows_verified=len(samples))

def alias(folder,canonical,legacy):
    src=folder/canonical;dest=folder/legacy
    if dest.exists():
        if os.path.samefile(src,dest):return
        assert sha(src)==sha(dest),'existing alias content differs'
        dest.unlink()
    os.link(src,dest)

def verify_raw(folder,name):
    source=folder/'raw'/name
    record=json.loads((source.parent/(name+'.download.json')).read_text())
    assert source.stat().st_size==record['bytes']
    assert sha(source)==record['sha256']
    return source,record

def hdf5_dataset(name,source_name,n,d,q,angular):
    folder=ROOT/'dataset'/name
    source,download=verify_raw(folder,source_name)
    record=dict(name=name,source=download,metric='cosine via unit-normalized squared L2' if angular else 'squared L2',
                original_metric='angular/cosine' if angular else 'euclidean',normalized=angular,
                benchmark_metric_argument='l2',files={})
    with h5py.File(source,'r') as h:
        assert h['train'].shape==(n,d) and h['test'].shape==(q,d)
        assert h['neighbors'].shape==(q,100)
        for key,out,kind in [('train','base.fvecs','<f4'),('test','query.fvecs','<f4'),('neighbors','groundtruth.ivecs','<i4')]:
            if key=='neighbors':assert np.all((h[key][:]>=0)&(h[key][:]<n))
            record['files'][key]=write_vectors(h[key],folder/out,kind,angular and key!='neighbors')
        record['hdf5_attributes']={key:str(value) for key,value in h.attrs.items()}
    record['ground_truth']='Provided top-100 IDs for the full source base. Recompute if using a smaller base subset; angular ranking is represented through normalization.'
    finalize(folder,record)

def deep():
    folder=ROOT/'dataset/deep1m'
    record=dict(name='deep1m',family='Official Yandex DEEP1B prefix',metric='squared L2',normalized=False,
                benchmark_metric_argument='l2',files={},sources={})
    for key,name,count,out in [('train','base.1M.prefix.fbin',1000000,'base.fvecs'),('test','query.public.10K.fbin',10000,'query.fvecs')]:
        source,download=verify_raw(folder,name)
        with source.open('rb') as f:n,d=struct.unpack('<II',f.read(8))
        assert d==96 and n>=count and source.stat().st_size==8+count*d*4
        if key=='train':assert n==1000000000
        data=np.memmap(source,dtype='<f4',mode='r',offset=8,shape=(count,d))
        record['files'][key]=write_vectors(data,folder/out)
        record['sources'][key]=dict(download,declared_count=n,used_count=count,dimension=d)
    record['ground_truth']='Not supplied for the 1M prefix. Existing benchmark computes exact ground truth for the requested queries and base size. Do not use 1B/10M ground truth for this 1M subset.'
    record['scaling']='To extend to N rows, fetch bytes 0..(8+N*96*4-1) from the same base.1B.fbin; preserve query IDs and recompute exact neighbors for N.'
    finalize(folder,record)

def finalize(folder,record):
    for canonical,legacy in [('base.fvecs','sift_base.fvecs'),('query.fvecs','sift_query.fvecs')]:alias(folder,canonical,legacy)
    if (folder/'groundtruth.ivecs').exists():alias(folder,'groundtruth.ivecs','sift_groundtruth.ivecs')
    record['legacy_aliases']='sift_base/query.fvecs are hard links for existing loader compatibility; the contents are this named dataset, not SIFT.'
    record['status']='PASS'
    (folder/'provenance.json').write_text(json.dumps(record,indent=2)+'\n')
    n,d=record['files']['train']['shape'];q=record['files']['test']['shape'][0]
    text=(f'# {record["name"]}\n\nBase: {n:,} vectors; dimension: {d}; queries: {q:,}.\n\n'
          f'Metric: {record["metric"]}. Existing benchmark argument: `--metric l2`.\n\n'
          f'{record["ground_truth"]}\n\n{record["legacy_aliases"]}\n\n'
          'Original downloads are retained in `raw/`. Source URLs, verified HTTP ranges, ETags, '
          'SHA-256 fingerprints, preprocessing and converted shapes are in `provenance.json`.\n\n'
          'Dataset preparation does not establish an experiment result. Build fresh indexes for '
          'this dataset and recalibrate all search parameters; do not reuse SIFT work points.\n')
    if record.get('scaling'):text+='\n'+record['scaling']+'\n'
    (folder/'README.md').write_text(text)
    print(json.dumps(dict(dataset=record['name'],status='PASS',base=n,dimension=d,queries=q)),flush=True)

def main():
    hdf5_dataset('gist1m','gist-960-euclidean.hdf5',1000000,960,1000,False)
    hdf5_dataset('glove100','glove-100-angular.hdf5',1183514,100,10000,True)
    deep()
    records={name:json.loads((ROOT/'dataset'/name/'provenance.json').read_text()) for name in ['gist1m','glove100','deep1m']}
    (ROOT/'dataset/extra_datasets_validation.json').write_text(json.dumps(dict(status='PASS',datasets=records),indent=2)+'\n')

if __name__=='__main__':main()
