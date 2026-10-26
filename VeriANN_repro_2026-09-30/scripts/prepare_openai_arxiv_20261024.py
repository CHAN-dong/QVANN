"""Fetch exact BigANN ArXiv float32 bytes; deterministic 1M prefix, no normalization."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import time
import numpy as np
import work9_resource_matched as u

R=u.ROOT;F=R/'dataset/openai_arxiv1m';S=R/'experiments/openai-arxiv-20261024';D=1536;N=1000000
URL='https://comp21storage.z5.web.core.windows.net/arxiv-openaiv2-2M'
F.mkdir(parents=True,exist_ok=True);S.mkdir(parents=True,exist_ok=True);(F/'raw').mkdir(exist_ok=True)
def win(path):return 'D:'+str(path).split('/mnt/d',1)[1].replace('/','\\')
def fetch(name,bytes_needed):
    dest=F/'raw'/name
    if dest.exists():assert dest.stat().st_size==bytes_needed;return dest
    chunk=64*2**20;parts=[]
    def get(i):
        start=i*chunk;end=min(bytes_needed,(i+1)*chunk)-1;p=F/'raw'/f'{name}.chunk-{i:04d}';expected=end-start+1
        if p.exists() and p.stat().st_size==expected:return p
        cmd=['/mnt/c/Windows/System32/curl.exe','-x','http://127.0.0.1:7897','--fail','--location','--silent','--show-error','--retry','4','--connect-timeout','20','--max-time','600','--range',f'{start}-{end}','--output',win(p)+'.part',URL+'/'+name]
        subprocess.run(cmd,check=True);tmp=p.with_name(p.name+'.part');assert tmp.stat().st_size==expected,(name,i,tmp.stat().st_size,expected);tmp.rename(p)
        print('DOWNLOADED',name,i,expected,flush=True);return p
    with concurrent.futures.ThreadPoolExecutor(8) as pool:parts=list(pool.map(get,range((bytes_needed+chunk-1)//chunk)))
    tmp=dest.with_suffix('.assembling')
    with tmp.open('wb') as out:
        for p in parts:
            with p.open('rb') as f:shutil.copyfileobj(f,out,16*2**20)
    assert tmp.stat().st_size==bytes_needed;tmp.rename(dest);return dest
def main():
    if (F/'provenance.json').exists():
        m=u.read(F/'provenance.json')
        for v in m['files'].values():assert u.sha(F/v['file'])==v['sha256']
        print('PREPARED_DATA_VERIFIED',flush=True);return
    base=fetch('openai_base.bin',8+N*D*4);query=fetch('openai_query.bin',8+20000*D*4)
    metadata=dict(name='OpenAI-ArXiv-1M',N=N,D=D,metric='squared L2, unchanged float32 embeddings',source_dataset_N=2321096,query_count=20000,selection='first1000000 source base records, all independent source query records; no dimension reduction or normalization',source_url=URL,source_code='https://github.com/harsha-simhadri/big-ann-benchmarks/blob/main/benchmark/datasets.py',files={},raw={})
    for source,name,count in [(base,'sift_base.fvecs',N),(query,'sift_query.fvecs',20000)]:
        with source.open('rb') as f:n,d=struct.unpack('<II',f.read(8))
        assert d==D and n==(2321096 if count==N else 20000),(n,d)
        values=np.memmap(source,dtype='<f4',mode='r',offset=8,shape=(count,D));dest=F/name;tmp=dest.with_suffix('.part')
        digest=hashlib.sha256();vmin=float('inf');vmax=-float('inf')
        with tmp.open('wb') as out:
            for start in range(0,count,4096):
                v=np.asarray(values[start:start+4096]);assert np.isfinite(v).all();digest.update(v.tobytes());vmin=min(vmin,float(v.min()));vmax=max(vmax,float(v.max()))
                rows=np.empty((len(v),D+1),dtype='<f4');rows[:,0].view('<u4')[:]=D;rows[:,1:]=v;out.write(rows.tobytes())
        tmp.rename(dest);metadata['files'][name]=dict(file=name,sha256=u.sha(dest),vector_bytes_sha256=digest.hexdigest(),rows=count,D=D,vmin=vmin,vmax=vmax)
        metadata['raw'][source.name]=dict(bytes=source.stat().st_size,sha256=u.sha(source),original_header_N=n,D=d,cropped=count!=n)
        print('CONVERTED',name,count,flush=True)
    u.write(F/'provenance.json',metadata);print('PREPARED',json.dumps(metadata),flush=True)
if __name__=='__main__':main()
