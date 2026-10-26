"""Sequential, checkpointed author-code proof experiments for every work9 panel."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import resource
from work9_calibrate import DATA, ROOT, REPRO, STUDY, CACHE, event
from work9_fill import refresh
PROOF_THREADS='1'


def run(command,log,large_config=None):
    event(action='command_start',command=list(map(str,command)),log=str(log))
    env=dict(os.environ,RAYON_NUM_THREADS='1',OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',PYTHONUNBUFFERED='1')
    if PROOF_THREADS=='native':env.pop('RAYON_NUM_THREADS',None)
    else:env['RAYON_NUM_THREADS']=PROOF_THREADS
    with log.open('a') as f:
        def memory_limit():
            resource.setrlimit(resource.RLIMIT_CORE,(0,0))
            resource.setrlimit(resource.RLIMIT_AS,(50*2**30,50*2**30))
        status=subprocess.run(list(map(str,command)),cwd=REPRO/'work9-author',env=env,
                              stdout=f,stderr=subprocess.STDOUT,preexec_fn=memory_limit if large_config else None)
    event(action='command_done',returncode=status.returncode,log=str(log))
    if status.returncode:
        text=log.read_text(errors='replace')
        memory_failure=any(x in text.lower() for x in ['memory allocation','memoryerror','cannot allocate memory','unable to allocate'])
        if large_config and memory_failure:
            job=large_config['job'];out=ROOT/'results/work9-v3db-20261016'/job;out.mkdir(parents=True,exist_ok=True)
            result=dict(status='resource_limit',output=str(out),actual_attempt=True,
                        address_space_limit_GiB=50,reason='actual author circuit construction/proving attempt failed allocation under 50 GiB process address-space limit',
                        log=str(log),**large_config)
            (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
            return
        raise RuntimeError(f'Command failed: {log}')


def main():
    global PROOF_THREADS
    p=argparse.ArgumentParser()
    p.add_argument('--sampling',choices=['matched','20x3','preview'],default='20x3')
    p.add_argument('--main-only',action='store_true')
    p.add_argument('--proof-threads')
    p.add_argument('--datasets', nargs='+', choices=list(DATA))
    args=p.parse_args()
    replaced=(ROOT/'experiments/work9-nytimes-20261017/replacement.json').exists()
    default=[k for k in DATA if k!=('gist1m' if replaced else 'nytimes256')]
    active={k:v for k,v in DATA.items() if k in (args.datasets or default)}
    previous=json.loads((STUDY/'protocol.json').read_text()) if (STUDY/'protocol.json').exists() else {}
    PROOF_THREADS=args.proof_threads or previous.get('proof_threads','1')
    assert PROOF_THREADS=='native' or int(PROOF_THREADS)>0
    STUDY.mkdir(parents=True,exist_ok=True)
    protocol=dict(sampling=args.sampling,query_offset=100,
                  repeats=1 if args.sampling=='preview' else 3,
                  search_threads=1,proof_threads=PROOF_THREADS,gate_preflight_max=2000000,datasets=list(active),
                  immutable_author_commit='a82ac732828591bd49b83d8f31c56fa4bd1cd9b2',
                  adapter_patch=str(REPRO/'work9-adapter.patch'))
    (STUDY/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n')
    while not (STUDY/'tuning-complete.json').exists():
        event(action='waiting_tuned_models')
        time.sleep(20)
    selected=json.loads((STUDY/'selected-models.json').read_text())
    def measure(dataset,n,source=None,main=False,overall_attempt=False):
        source=source or dataset
        chosen_m=selected[dataset]['M']
        chosen_k=selected[dataset].get('K',256)
        folder=CACHE/(f'{source}-{n}'+(f'-M{chosen_m}' if chosen_m!=DATA[dataset][4] else '')+(f'-K{chosen_k}' if chosen_k!=256 else ''))
        if not (folder/'calibration.json').exists():
            run([sys.executable,REPRO/'scripts/work9_calibrate.py','--dataset',dataset,
                 '--N',n,'--source',source,'--M',chosen_m,'--K',chosen_k],STUDY/f'calibrate-{source}-{n}.log')
        cal=json.loads((folder/'calibration.json').read_text())
        cfg=cal['model']['config']
        points=[x for x in cal['points'] if x['k']==10]
        reaches=[x for x in points if x['recall']>=.95-1e-12]
        probe=min(reaches,key=lambda x:x['n_probe'])['n_probe'] if reaches else cfg['n_list']//128
        jobs={(probe,10)}
        if main:
            jobs|={(probe,k) for k in [1,5,10,20,50,100]}
            for target in [.60,.65,.70,.75,.80,.85,.90,.95,.97,.98,.99]:
                choices=[x for x in points if x['recall']>=target-1e-12]
                if choices:jobs.add((min(choices,key=lambda x:x['n_probe'])['n_probe'],10))
        count=1 if args.sampling=='preview' else (100 if args.sampling=='matched' and dataset=='sift1m' else 20)
        repeats=protocol['repeats']
        for probes,k in sorted(jobs,key=lambda x:(x!=(probe,10),x[1]!=10,x[0],x[1])):
            command=[sys.executable,REPRO/'scripts/work9_measure.py','--model',folder.name,
                     '--probe',probes,'--k',k,'--queries',count,'--repeats',repeats]
            log=STUDY/f'formal-{folder.name}-p{probes}-k{k}-q{count}-r{repeats}-t{PROOF_THREADS}.log'
            large_config=dict(job=folder.name+f'-p{probes}-k{k}-q{count}-r{repeats}-t{PROOF_THREADS}',
                              config=cfg,n_probe=probes,k=k,proof_threads=PROOF_THREADS)
            if (main or overall_attempt) and probes==probe and k==10:
                command+=['--attempt-large']
            run(command,log,large_config)
            refresh()
    # Check all four overall settings before sweeping the other panels.
    for key,meta in active.items():
        measure(key,meta[1],overall_attempt=True)
    for key,meta in active.items():
        measure(key,meta[1],main=True)
    if not args.main_only:
        for key,meta in active.items():
            sizes=[100000,500000,1000000]+([5000000,10000000] if key in ['sift1m','deep1m'] else [])
            for n in sizes:
                if key=='nytimes256' and n>meta[1]:continue
                source='bigann10m' if key=='sift1m' else ('deep10m' if key=='deep1m' and n>1000000 else key)
                measure(key,n,source)
    refresh()
    finished='RUN_FINISHED'+('-'+'-'.join(active) if args.datasets else '')+'.json'
    (STUDY/finished).write_text(json.dumps(dict(status='planned_jobs_for_listed_datasets_executed',datasets=list(active),protocol=protocol),indent=2)+'\n')


if __name__=='__main__':main()
