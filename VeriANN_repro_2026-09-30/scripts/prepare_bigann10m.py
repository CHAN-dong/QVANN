"""Convert an exact HTTP-range prefix of the official BIGANN base to fvecs."""
import hashlib
import json
from pathlib import Path
import struct
import numpy as np

root=Path(__file__).resolve().parents[1]
folder=root/'dataset/bigann10m'
folder.mkdir(parents=True,exist_ok=True)
def checksum(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while block:=f.read(8*1024*1024):h.update(block)
    return h.hexdigest()

metadata={'family':'BIGANN / SIFT1B prefix, distinct from SIFT1M',
          'base_url':'https://dl.fbaipublicfiles.com/billion-scale-ann-benchmarks/bigann/base.1B.u8bin',
          'query_url':'https://dl.fbaipublicfiles.com/billion-scale-ann-benchmarks/bigann/query.public.10K.u8bin',
          'requested_range':'bytes=0-1280000007'}
for name,output,wanted in [('base.10M.prefix.u8bin','sift_base.fvecs',10000000),
                          ('query.public.10K.u8bin','sift_query.fvecs',10000)]:
    source=folder/name
    with source.open('rb') as f:
        declared,d=struct.unpack('<II',f.read(8))
    if d!=128 or source.stat().st_size!=8+wanted*d or declared<wanted:
        raise SystemExit('Invalid/truncated BIGANN prefix: '+str(source))
    data=np.memmap(source,dtype=np.uint8,mode='r',offset=8,shape=(wanted,d))
    target=folder/output
    temporary=target.with_suffix(target.suffix+'.part')
    with temporary.open('wb') as f:
        for start in range(0,wanted,32768):
            count=min(32768,wanted-start)
            rows=np.empty((count,d+1),dtype='<f4')
            rows.view('<i4')[:,0]=d
            rows[:,1:]=data[start:start+count]
            f.write(rows.tobytes())
            if start%1048576==0:print(output,start,wanted,flush=True)
    temporary.replace(target)
    metadata[output]={'declared_source_count':declared,'used_count':wanted,'dimension':d,
                      'source_sha256':checksum(source),'converted_sha256':checksum(target),
                      'converted_bytes':target.stat().st_size}
(folder/'provenance.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(json.dumps(metadata,indent=2),flush=True)
