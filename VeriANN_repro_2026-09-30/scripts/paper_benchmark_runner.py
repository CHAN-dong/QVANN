"""Build and execute an independent paper reproduction; called by each run.py."""
import argparse
from datetime import datetime
import json
import hashlib
import os
from pathlib import Path
import platform
import subprocess
import sys

def main(method):
    root = Path(__file__).resolve().parents[1]
    folder = root / method
    parser = argparse.ArgumentParser(description=method + ' algorithmic reproduction')
    parser.add_argument('--dataset', default=str(root / 'dataset/sift'))
    for name, default in [('base',100000),('queries',100),('query-offset',0),('build-threads',1),('M',8),('ef-construction',100),
                          ('shard-size',10000),('bucket-size',20000),('seed',42),('repeats',1)]:
        parser.add_argument('--'+name, type=int, default=default)
    for name,default in [('k','10'),('ef-search','50'),('probes','3'),('metric','l2'),
                         ('proof','multiproof'),('lsh-width','256')]:
        parser.add_argument('--'+name, default=default)
    parser.add_argument('--output')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    out = Path(args.output).resolve() if args.output else folder / 'results' / datetime.now().strftime('%Y%m%d-%H%M%S')
    if out.exists() and any(out.iterdir()):
        parser.error('output directory must be empty; refusing to mix old CSV with this run')
    out.mkdir(parents=True, exist_ok=True)
    (folder / 'build').mkdir(exist_ok=True)
    binary = folder / 'build' / ('annproof' if method.startswith('ANNProof') else 'vlah')
    source = folder / ('annproof.cpp' if method.startswith('ANNProof') else 'vlah.cpp')
    faiss = root / 'third_party/LearnedEarlyTer-20SIGMOD'
    runtime = root / 'third_party/runtime/lib'
    upstream = root / 'ANNProof-24FGCS/upstream/source/similarity_search'
    compiler = os.environ.get('CXX', 'g++')
    build = [compiler,'-O3','-DNDEBUG','-std=c++17','-fopenmp','-Wall','-Wextra',
             '-I'+str(root/'src'),'-I'+str(root/'third_party'),'-I'+str(faiss),
             '-I'+str(faiss/'include'),'-I'+str(upstream/'include'),str(source),
             str(upstream/'src/Keccak256.cc'),'-o',str(binary),'-L'+str(faiss),
             '-L'+str(faiss/'lib'),'-lfaiss','-l_lightgbm','-lcrypto','-lpthread','-ldl']
    environment = os.environ.copy()
    environment['OMP_NUM_THREADS'] = environment['OPENBLAS_NUM_THREADS'] = '1'
    environment['LD_LIBRARY_PATH'] = ':'.join(map(str,[runtime,faiss,faiss/'lib']))
    command = [str(binary),'--dataset',str(Path(args.dataset).resolve()),'--output',str(out),
               '--cache',str(folder/'cache')]
    for name,value in vars(args).items():
        if name not in {'dataset','output','skip_build'}:
            command += ['--'+name.replace('_','-'),str(value)]
    metadata = {'method':method,'platform':platform.platform(),'python':sys.version,
                'compiler':subprocess.check_output([compiler,'--version'],text=True).splitlines()[0],
                'command':command,'build_command':build,'threads':1,'skip_build':args.skip_build}
    source_files = [source,root/'src/paper_repro_common.hpp',root/'src/verifiable_ann_common.hpp',
                    upstream/'include/Keccak256.h',upstream/'src/Keccak256.cc']
    metadata['source_sha256'] = {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in source_files}
    (out/'environment.json').write_text(json.dumps(metadata,indent=2)+'\n')
    if not args.skip_build:
        print('[build] '+method, flush=True)
        with (out/'build.log').open('w') as log:
            status = subprocess.run(build,env=environment,stdout=log,stderr=subprocess.STDOUT)
        if status.returncode:
            print((out/'build.log').read_text()[-12000:])
            raise SystemExit(status.returncode)
    print('[run] '+method+' -> '+str(out),flush=True)
    with (out/'run.log').open('w') as log:
        status = subprocess.run(['/usr/bin/time','-v','-o',str(out/'resources.txt')]+command,
                                env=environment,cwd=folder,stdout=log,stderr=subprocess.STDOUT)
    print((out/'run.log').read_text(),end='',flush=True)
    if status.returncode:
        raise SystemExit(status.returncode)
    import csv
    with (out/'per_query.csv').open() as f:
        rows = list(csv.DictReader(f))
    if not rows or any(r['verified']!='1' for r in rows):
        raise SystemExit('FAIL: missing rows or rejected honest queries')
    with (out/'tamper_tests.csv').open() as f:
        attacks = list(csv.DictReader(f))
    if not attacks or any(r['rejected']!='1' for r in attacks):
        raise SystemExit('FAIL: missing/failed tamper tests')
    # Binary output of the first query must have exactly the reported wire size.
    if (out/'query0.vo').stat().st_size != int(rows[0]['vo_bytes']):
        raise SystemExit('FAIL: VO accounting mismatch')
    print(f'PASS {len(rows)} query measurements; {len(attacks)} tamper cases; actual wire size checked.')
    (out/'PASS.txt').write_text(f'{len(rows)} honest measurements, {len(attacks)} attacks rejected; wire size checked.\n')

