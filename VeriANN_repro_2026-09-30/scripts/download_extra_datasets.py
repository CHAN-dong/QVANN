"""Resumable exact-range downloads of the three agreed ANN datasets."""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
SPECS=[
    ('gist1m','gist-960-euclidean.hdf5','https://ann-benchmarks.com/gist-960-euclidean.hdf5',3844648288,3844648288),
    ('glove100','glove-100-angular.hdf5','https://ann-benchmarks.com/glove-100-angular.hdf5',485413888,485413888),
    ('deep1m','base.1M.prefix.fbin','https://storage.yandexcloud.net/yandex-research/ann-datasets/DEEP/base.1B.fbin',384000008,384000000008),
    ('deep1m','query.public.10K.fbin','https://storage.yandexcloud.net/yandex-research/ann-datasets/DEEP/query.public.10K.fbin',3840008,3840008),
]

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while b:=f.read(8*1024*1024):h.update(b)
    return h.hexdigest()

def main():
    curl=shutil.which('curl.exe') or shutil.which('curl')
    assert curl
    windows=curl.lower().endswith('.exe')
    def local(p):
        p=str(p.resolve())
        if windows and p.startswith('/mnt/'):return p[5].upper()+':/'+p[7:]
        return p
    chunk=64*1024*1024
    tasks=[];folders={};spec_items={}
    for folder,name,url,length,total in SPECS:
        dest=ROOT/'dataset'/folder/'raw';dest.mkdir(parents=True,exist_ok=True)
        parts=dest/(name+'.parts');parts.mkdir(exist_ok=True)
        items=[]
        for i,begin in enumerate(range(0,length,chunk)):
            item=dict(folder=folder,name=name,url=url,begin=begin,end=min(begin+chunk,length)-1,total=total,
                      path=parts/f'{i:04d}.part',header=parts/f'{i:04d}.headers')
            items.append(item);tasks.append(item)
        spec_items[(folder,name)]=items
    def headers(item):
        raw=item['header'].read_text(errors='replace')
        expected=f"bytes {item['begin']}-{item['end']}/{item['total']}"
        assert re.findall(r'(?im)^content-range:\s*([^\r\n]+)',raw)[-1].strip()==expected
        tag=re.findall(r'(?im)^etag:\s*([^\r\n]+)',raw)[-1].strip()
        assert tag
        assert item['path'].stat().st_size==item['end']-item['begin']+1
        return tag
    def download(item):
        try:headers(item);return item
        except (FileNotFoundError,AssertionError,IndexError):pass
        # Separate range cache keys avoid proxy caches stalling repeated requests
        # to the 384 GB DEEP object. Canonical source and ETag stay unchanged.
        request_url=item['url']
        if item['folder'].startswith('deep'):request_url+=f"?range={item['begin']}-{item['end']}"
        cmd=[curl,'--silent','--show-error','--fail','--location','--retry','3','--retry-all-errors',
             '--connect-timeout','20','--max-time','180','--speed-time','40','--speed-limit','1024',
             '--max-filesize',str(item['end']-item['begin']+1),'--range',f"{item['begin']}-{item['end']}",
             '--dump-header',local(item['header']),'--output',local(item['path']),request_url]
        if windows:cmd+=['--proxy','http://127.0.0.1:7897']
        # Retry stalled DEEP proxy cache entries with a fresh request key. The
        # canonical URL, exact byte range and final common ETag are still checked.
        attempts=4 if item['folder'].startswith('deep') else 1
        for attempt in range(attempts):
            actual=list(cmd)
            if attempts>1:
                actual[actual.index(request_url)]=request_url+f'&request={time.time_ns()}'
                actual[actual.index('--retry')+1]='0'
                actual[actual.index('--max-time')+1]='90'
            result=subprocess.run(actual,capture_output=True,text=True)
            if result.returncode==0:
                headers(item);return item
        raise RuntimeError(f"{item['name']} {item['begin']}: {result.stderr[-1000:]}")
    started=time.time();done=0
    print(json.dumps({'action':'start','chunks':len(tasks),'download_bytes':sum(s[3] for s in SPECS)}),flush=True)
    errors=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        futures=[pool.submit(download,item) for item in tasks]
        for future in concurrent.futures.as_completed(futures):
            try:item=future.result()
            except Exception as exc:
                errors.append(str(exc));print(json.dumps({'action':'chunk_failed','error':str(exc)}),flush=True);continue
            done+=1
            if done%5==0 or done==len(tasks):print(json.dumps({'action':'download_progress','chunks_done':done,'chunks_total':len(tasks),'elapsed_s':round(time.time()-started)}),flush=True)
    if errors:raise RuntimeError('Some chunks failed; rerun to resume. '+str(errors))
    for folder,name,url,length,total in SPECS:
        items=spec_items[(folder,name)]
        tags={headers(item) for item in items};assert len(tags)==1,'source changed between ranges'
        dest=ROOT/'dataset'/folder/'raw';out=dest/name;tmp=out.with_suffix(out.suffix+'.merging')
        with tmp.open('wb') as f:
            for item in items:
                with item['path'].open('rb') as part:shutil.copyfileobj(part,f,8*1024*1024)
        assert tmp.stat().st_size==length
        tmp.replace(out)
        record=dict(url=url,bytes=length,remote_total_bytes=total,etag=next(iter(tags)),sha256=sha(out),
                    requested_range=f'bytes=0-{length-1}',http_ranges_verified=True,
                    range_query_cache_keys=folder.startswith('deep'))
        (dest/(name+'.download.json')).write_text(json.dumps(record,indent=2)+'\n')
        print(json.dumps({'action':'file_complete','file':str(out),**record}),flush=True)
    print('PASS: all exact ranges, lengths and source ETags checked.',flush=True)

if __name__=='__main__':main()
