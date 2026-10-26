"""Extend the official DEEP prefix without modifying the existing DEEP1M files."""
import json
import os
import struct
import numpy as np
import download_extra_datasets as download
from prepare_extra_datasets import write_vectors, alias, finalize

ROOT=download.ROOT
FOLDER=ROOT/'dataset/deep10m'

def main():
    FOLDER.mkdir(parents=True,exist_ok=True)
    if (FOLDER/'validation.json').exists():
        record=json.loads((FOLDER/'validation.json').read_text())
        assert record['status']=='PASS'
        for name,value in record['sha256'].items():assert download.sha(FOLDER/name)==value
        print('PASS: existing DEEP10M files rechecked',flush=True)
        return
    raw=FOLDER/'raw';raw.mkdir(exist_ok=True)
    name='base.10M.prefix.fbin';parts=raw/(name+'.parts');parts.mkdir(exist_ok=True)
    old=ROOT/'dataset/deep1m/raw/base.1M.prefix.fbin.parts'
    # Only complete 64 MiB chunks have identical ranges in the 1M and 10M downloads.
    for i in range(5):
        for ext in ['part','headers']:
            src=old/f'{i:04d}.{ext}';dst=parts/src.name
            if not dst.exists():os.link(src,dst)
    source=raw/name
    if not (raw/(name+'.download.json')).exists():
        download.SPECS=[('deep10m',name,'https://storage.yandexcloud.net/yandex-research/ann-datasets/DEEP/base.1B.fbin',3840000008,384000000008)]
        download.main()
    info=json.loads((raw/(name+'.download.json')).read_text())
    assert source.stat().st_size==3840000008 and download.sha(source)==info['sha256']
    old_info=json.loads((ROOT/'dataset/deep1m/raw/base.1M.prefix.fbin.download.json').read_text())
    assert info['etag']==old_info['etag'],'DEEP source version changed'
    with source.open('rb') as f:assert struct.unpack('<II',f.read(8))==(1000000000,96)
    vectors=np.memmap(source,dtype='<f4',mode='r',offset=8,shape=(10000000,96))
    record=dict(name='deep10m',family='Official Yandex DEEP1B first 10M',metric='squared L2',
                normalized=False,benchmark_metric_argument='l2',files={},sources={'train':info})
    record['files']['train']=write_vectors(vectors,FOLDER/'base.fvecs')
    old_query=ROOT/'dataset/deep1m/query.fvecs'
    if not (FOLDER/'query.fvecs').exists():os.link(old_query,FOLDER/'query.fvecs')
    qrecord=json.loads((ROOT/'dataset/deep1m/provenance.json').read_text())
    record['files']['test']=qrecord['files']['test']
    record['sources']['test']=qrecord['sources']['test']
    record['ground_truth']='Computed exactly by each benchmark for N=5M/10M and the same selected query IDs. Never reuse 1M neighbors.'
    finalize(FOLDER,record)
    # The established 1M experiment and the two added scale points use identical prefixes and queries.
    import hashlib
    h=hashlib.sha256()
    with (FOLDER/'base.fvecs').open('rb') as f:
        remaining=388000000
        while remaining:
            data=f.read(min(8*1024*1024,remaining));assert data
            h.update(data);remaining-=len(data)
    assert h.hexdigest()==qrecord['files']['train']['sha256']
    assert download.sha(FOLDER/'query.fvecs')==qrecord['files']['test']['sha256']
    validation=dict(status='PASS',base=10000000,dimension=96,queries=10000,
                    metric='squared L2',existing_1m_prefix_identical=True,query_source_identical=True,
                    sha256={name:download.sha(FOLDER/name) for name in ['base.fvecs','query.fvecs']})
    (FOLDER/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
    print(json.dumps(validation),flush=True)

if __name__=='__main__':main()
