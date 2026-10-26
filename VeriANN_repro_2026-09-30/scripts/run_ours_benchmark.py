"""Build/run the optimized VeriANN wire protocol, with evidence validation."""
import argparse
import csv
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--dataset',type=Path,default=ROOT/'dataset/sift1m')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cache',type=Path,default=ROOT/'ANNProof-24FGCS/cache')
    for name,default in [('base',1000000),('queries',100),('query-offset',100),('repeats',3),
                         ('M',8),('ef-construction',100),('build-threads',8)]:
        parser.add_argument('--'+name,type=int,default=default)
    parser.add_argument('--k',default='10')
    parser.add_argument('--E',default='64')
    parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--reference-check',action='store_true')
    parser.add_argument('--index',type=Path)
    parser.add_argument('--source',type=Path,default=ROOT/'src/experiments/veriann_optimized_benchmark.cpp')
    parser.add_argument('--binary',type=Path,default=ROOT/'build/veriann_optimized_benchmark')
    args=parser.parse_args()
    output=args.output.resolve()
    if output.exists() and any(output.iterdir()):parser.error('output must be empty')
    output.mkdir(parents=True,exist_ok=True)
    binary=args.binary.resolve();source=args.source.resolve()
    faiss=ROOT/'third_party/LearnedEarlyTer-20SIGMOD'
    runtime=ROOT/'third_party/runtime/lib'
    upstream=ROOT/'ANNProof-24FGCS/upstream/source/similarity_search'
    command=['g++','-O3','-DNDEBUG','-std=c++17','-fopenmp','-Wall','-Wextra',
             '-I'+str(ROOT/'src'),'-I'+str(ROOT/'third_party'),'-I'+str(faiss),
             '-I'+str(faiss/'include'),str(source),
             str(upstream/'src/Keccak256.cc'),'-I'+str(upstream/'include'),'-o',str(binary),
             '-L'+str(faiss),'-L'+str(faiss/'lib'),'-lfaiss','-l_lightgbm','-lcrypto','-lpthread','-ldl']
    env=os.environ.copy()
    env['OMP_NUM_THREADS']=env['OPENBLAS_NUM_THREADS']='1'
    env['LD_LIBRARY_PATH']=':'.join(map(str,[runtime,faiss,faiss/'lib']))
    if args.reference_check:env['VERIANN_REFERENCE_CHECK']='1'
    if args.index:env['VERIANN_INDEX_PATH']=str(args.index.resolve())
    if not args.skip_build:
        with (output/'build.log').open('w') as log:
            built=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
        if built.returncode:
            print((output/'build.log').read_text()[-16000:]);raise SystemExit(built.returncode)
    run=[str(binary),'--dataset',str(args.dataset.resolve()),'--output',str(output),
         '--cache',str(args.cache.resolve()),'--ef-search',args.E,'--k',args.k]
    for name in ['base','queries','query_offset','repeats','M','ef_construction','build_threads']:
        run+=['--'+name.replace('_','-'),str(getattr(args,name))]
    files=[source,ROOT/'src/experiments/veriann_optimized_benchmark.cpp',ROOT/'src/paper_repro_common.hpp',
           ROOT/'src/verifiable_ann_common.hpp',*sorted((ROOT/'src/experiments').glob('*hnsw*.cpp')),
           ROOT/'src/experiments/expansion_centric_merkle.cpp',ROOT/'src/experiments/refinement_merkle_ordering.cpp']
    if source!=ROOT/'src/experiments/veriann_optimized_benchmark.cpp':files.append(ROOT/'src/veriann_query_lut.hpp')
    metadata={'method':'VeriANN-optimized','date':datetime.now().isoformat(),'platform':platform.platform(),
              'python':sys.version,'command':run,'build_command':command,'query_threads':1,
              'source_sha256':{str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},
              'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
              'index_override':str(args.index.resolve()) if args.index else None,
              'reference_checks':args.reference_check}
    (output/'environment.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('[run] '+str(output),flush=True)
    with (output/'run.log').open('w') as log:
        process=subprocess.run(['/usr/bin/time','-v','-o',str(output/'resources.txt')]+run,
                               cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
    if process.returncode:
        print((output/'run.log').read_text()[-10000:]);raise SystemExit(process.returncode)
    with (output/'per_query.csv').open() as f:rows=list(csv.DictReader(f))
    expected=len(args.k.split(','))*len(args.E.split(','))*args.queries*args.repeats
    assert len(rows)==expected and all(r['verified']=='1' for r in rows)
    assert {int(r['query']) for r in rows}==set(range(args.query_offset,args.query_offset+args.queries))
    assert (output/'query0.vo').stat().st_size==int(rows[0]['vo_bytes'])
    with (output/'tamper_tests.csv').open() as f:attacks=list(csv.DictReader(f))
    assert len(attacks)>=10 and all(r['rejected']=='1' for r in attacks)
    (output/'PASS.txt').write_text(f'{len(rows)} honest measurements; {len(attacks)} tamper tests rejected; wire bytes checked.\n')
    print((output/'summary.csv').read_text(),flush=True)


if __name__=='__main__':main()
