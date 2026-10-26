"""Create a portable, source-first QVANN ZIP, and validate a fresh extraction."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
STUDY=ROOT/'experiments/qvann-package-20261023'
STAGE=STUDY/'staging/QVANN'
ZIP=ROOT.parent.parent/'QVANN_code_2026-10-23.zip'

def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def write(p,text):
    p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8',newline='\n')
def put(source,target):
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)

BUILD=r'''#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAISS="$ROOT/third_party/LearnedEarlyTer-20SIGMOD"
mkdir -p "$ROOT/build"
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys
r=Path(sys.argv[1]);code=(r/'src/qvann_benchmark.cpp').read_text()
signature='int main(int argc,char**argv)try{'
assert code.count(signature)==1
(r/'build/qvann_protocol_include.cpp').write_text(code.split(signature)[0])
PY
FLAGS=(-O3 -DNDEBUG -std=c++17 -fopenmp -I"$ROOT/src" -I"$ROOT/build" -I"$ROOT/third_party" -I"$FAISS" -I"$FAISS/include" -I"$ROOT/third_party/keccak")
LINK=(-L"$FAISS" -L"$FAISS/lib" '-Wl,-rpath,$ORIGIN/../third_party/LearnedEarlyTer-20SIGMOD' '-Wl,-rpath,$ORIGIN/../third_party/LearnedEarlyTer-20SIGMOD/lib' '-Wl,-rpath,$ORIGIN/../third_party/runtime/lib' -lfaiss -l_lightgbm -lcrypto -lpthread -ldl)
compile() {
  echo "[build] $2"
  "${CXX:-g++}" "${FLAGS[@]}" "$ROOT/$1" "$ROOT/third_party/keccak/Keccak256.cc" -o "$ROOT/build/$2" "${LINK[@]}"
}
compile src/qvann_benchmark.cpp qvann
if [[ "${1:-}" != "--core-only" ]]; then
  compile benchmarks/quantization_refinement_probe.cpp quantization_probe
  compile benchmarks/quantization_raw_disclosure_probe.cpp raw_probe
  compile src/experiments/work9_ablation.cpp ablation
fi
echo "[build] complete"
'''

RUN=r'''#!/usr/bin/env python3
"""Portable single-thread benchmark launcher; no result scaling or prediction."""
import argparse,json,os,subprocess,time
from pathlib import Path
R=Path(__file__).resolve().parents[1]
PRESETS={'sift':(1000000,8,100,128),'nytimes':(289761,16,200,256),'glove':(1183514,16,200,1536),'deep':(1000000,16,200,64)}
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--dataset',type=Path,required=True,help='directory containing sift_base.fvecs and sift_query.fvecs')
 p.add_argument('--mode',choices=['qvann','quantization','raw','ablation'],default='qvann')
 p.add_argument('--preset',choices=list(PRESETS));p.add_argument('--base',type=int);p.add_argument('--M',type=int);p.add_argument('--ef-construction',type=int);p.add_argument('--budget',type=int)
 p.add_argument('--queries',type=int,default=20);p.add_argument('--query-offset',type=int,default=100);p.add_argument('--repeats',type=int,default=3);p.add_argument('--k',type=int,default=10)
 p.add_argument('--cache',type=Path,default=R/'cache');p.add_argument('--output',type=Path);p.add_argument('--variants',default='1,2');p.add_argument('--nlist',type=int,default=256)
 a=p.parse_args();defaults=PRESETS.get(a.preset,(100000,16,100,64))
 n,m,ef,E=[value if value is not None else fallback for value,fallback in zip([a.base,a.M,a.ef_construction,a.budget],defaults)]
 if a.mode in ['quantization','raw'] and (a.queries,a.query_offset,a.repeats,a.k)!=(20,100,3,10):p.error('controlled refinement probes require20 queries, offset100,3 repeats,k10')
 for name in ['sift_base.fvecs','sift_query.fvecs']:
  if not (a.dataset/name).is_file():p.error('missing '+str(a.dataset/name))
 binary=R/'build'/dict(qvann='qvann',quantization='quantization_probe',raw='raw_probe',ablation='ablation')[a.mode]
 if not binary.is_file():p.error('run bash scripts/build.sh first')
 out=(a.output or R/'results'/(a.mode+'-'+time.strftime('%Y%m%d-%H%M%S'))).resolve()
 if out.exists() and any(out.iterdir()):p.error('output must be empty: '+str(out))
 out.mkdir(parents=True,exist_ok=True);a.cache.mkdir(parents=True,exist_ok=True)
 env=dict(os.environ)
 for key in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','RAYON_NUM_THREADS']:env[key]='1'
 env['LD_LIBRARY_PATH']=':'.join(map(str,[R/'third_party/runtime/lib',R/'third_party/LearnedEarlyTer-20SIGMOD',R/'third_party/LearnedEarlyTer-20SIGMOD/lib']))
 cmd=list(map(str,[binary,'--dataset',a.dataset.resolve(),'--base',n,'--M',m,'--ef-construction',ef,'--ef-search',E,'--queries',a.queries,'--query-offset',a.query_offset,'--repeats',a.repeats,'--k',a.k,'--cache',a.cache.resolve(),'--output',out,'--build-threads',1]))
 if a.mode=='ablation':cmd+=['--variants',a.variants,'--nlist',str(a.nlist)]
 (out/'invocation.json').write_text(json.dumps(dict(command=cmd,threads=1,mode=a.mode,note='Raw/quantization require the identical graph cache populated by a preceding qvann run.'),indent=2))
 subprocess.run(cmd,env=env,check=True)
 print('Results:',out)
if __name__=='__main__':main()
'''

SMOKE=r'''#!/usr/bin/env python3
"""Generate tiny synthetic fvecs and test real VOs, verifiers and tamper cases."""
import csv,json,random,struct,subprocess,sys,tempfile
from pathlib import Path
R=Path(__file__).resolve().parents[1]
def main():
 with tempfile.TemporaryDirectory(prefix='qvann-smoke-') as temp:
  root=Path(temp);data=root/'data';data.mkdir();rng=random.Random(20261023);d=16
  def save(path,n):
   with path.open('wb') as f:
    for _ in range(n):f.write(struct.pack('<i'+str(d)+'f',d,*[rng.gauss(0,1) for _ in range(d)]))
  save(data/'sift_base.fvecs',2048);save(data/'sift_query.fvecs',120)
  common=['--dataset',str(data),'--base','2048','--M','16','--ef-construction','100','--budget','32','--cache',str(root/'cache')]
  report={}
  for mode in ['qvann','quantization','raw','ablation']:
   out=root/mode;cmd=[sys.executable,str(R/'scripts/run_benchmark.py'),*common,'--mode',mode,'--output',str(out)]
   if mode=='ablation':cmd+=['--budget','16','--nlist','16']
   subprocess.run(cmd,check=True)
   tamper=list(out.rglob('tamper*.csv'));assert tamper
   rejected=0
   for path in tamper:
    rows=list(csv.DictReader(path.open()));assert rows and all(r['rejected']=='1' for r in rows);rejected+=len(rows)
   groups=[]
   for path in list(out.rglob('per_query.csv'))+list(out.rglob('samples.csv')):
    rows=list(csv.DictReader(path.open()));assert rows and all(r.get('verified',r.get('valid'))=='1' for r in rows)
    groups.extend(rows)
   assert groups
   if mode=='quantization':assert len(groups)==240
   else:assert len(groups)==(120 if mode=='ablation' else 60)
   report[mode]=dict(verified_samples=len(groups),rejected_tamper_cases=rejected)
  print(json.dumps(dict(passed=True,tests=report),indent=2))
if __name__=='__main__':main()
'''

README='''# QVANN source package

2026-10-23 snapshot of the current QVANN implementation (historical source and
CSV method names also use VeriANN). Core algorithm:

* SQ4 top-1 graph traversal with a unique expanded-node budget E.
* Progressive SQ4 → SQ6 → raw refinement, per-query lookup tables.
* Verification-decoupled traversal and direct-content expansion-centric Merkle
  authentication, BFS refinement ordering and canonical shared multiproofs.
* A real serialized VO and a verifier that uses trusted roots/metadata and the
  query, without access to the server graph or full vector/code arrays.

## Linux / WSL quick start

Requires Linux x86-64, C++17 g++, Python3, OpenSSL development headers, OpenMP.
For Ubuntu the basic build packages are `build-essential python3 libssl-dev`.
The matching prebuilt Faiss fork, LightGBM and math/C++ runtimes are included.

```bash
bash scripts/build.sh
python3 scripts/smoke_test.py
```

Four executables are built: `qvann`, `quantization_probe`, `raw_probe`, `ablation`.
`bash scripts/build.sh --core-only` builds only the main implementation.
Windows users run these commands in WSL. No Windows-native binaries are supplied.

## Run on your data

Prepare a directory with `sift_base.fvecs` and `sift_query.fvecs`. Every record is
little-endian int32 dimension followed by dimension float32 coordinates. The
original names are kept for CLI compatibility and also work for the other data.
Current code requires dimensions divisible by4. Use at least120 query vectors
for the default query IDs100..119. GloVe and NYTimes angular data must use the
same pre-normalized vectors as the original experiments; the benchmark uses L2.

```bash
python3 scripts/run_benchmark.py --dataset /path/to/sift --preset sift --output results/sift-default
# Reuse that graph cache for controlled comparisons:
python3 scripts/run_benchmark.py --dataset /path/to/sift --preset sift --mode quantization --output results/sift-quantization
python3 scripts/run_benchmark.py --dataset /path/to/sift --preset sift --mode raw --output results/sift-raw
```

Presets: SIFT N=1M/M8/efc100/E128; NYTimes N=289761/M16/efc200/E256;
GloVe N=1183514/M16/efc200/E1536; DEEP N=1M/M16/efc200/E64. Override with
`--base`, `--M`, `--ef-construction`, `--budget`. These work points are dataset
specific and do not guarantee the same recall on new data or newly built graphs.
Fresh graphs can differ from the historical cached indexes, which are excluded.
Default sampling is20 queries×3 repeats, single thread. Q includes search and
real VO generation; V includes decode/authentication/replay/ranking checks.
Sizes are serialized byte counts; KB in the paper tables means1000 bytes.

The quantization probe measures SQ4--raw, SQ4--SQ6--raw, SQ4--SQ8--raw and
SQ4--SQ6--SQ8--raw. It checks identical traces/answers across configurations and
byte-for-byte agreement of the SQ6 default with the original protocol.
Raw is a raw-disclosure control with the same SQ4 traversal: raw vectors are
authenticated by expansion-centric leaves and SQ4 codes are reconstructed by
the client. It is not a raw-distance HNSW traversal baseline. Quantization
controls require the exact20×3/k10/query-offset100 protocol.

## Contents and provenance

`src/qvann_benchmark.cpp` is the current measured LUT implementation with only
its include paths made relative. `src/experiments/` preserves the research
sources (including historical alternatives, not all enabled simultaneously).
`benchmarks/` contains the current controlled refinement experiments.
`docs/datasets/` preserves source/preprocessing descriptions; datasets are not
included. `PACKAGE_PROVENANCE.json` records source hashes and portability edits.

This package contains no Excel files, papers, datasets, cached indexes, measured
result archives, credentials, or workbook scripts that multiply/estimate/smooth
performance results. Benchmark launchers do not adjust measured values.
Synthetic smoke data are created temporarily at runtime and then removed.

Third-party code and libraries retain their existing notices; see
`THIRD_PARTY_NOTICES.md`. No new license is assigned to the original QVANN code
by this packaging step. The bundled libraries target the tested Linux x86-64
environment; use a compatible environment or rebuild dependencies for another
architecture. The final ZIP includes a file SHA256 manifest and validation report.
'''

def prepare():
    assert not STAGE.exists(),'Use the existing prepared package; do not overwrite it blindly.'
    STAGE.mkdir(parents=True);provenance=[]
    for p in (ROOT/'src').rglob('*'):
        if p.is_file() and p.suffix in ['.cpp','.hpp','.h']:
            target=STAGE/p.relative_to(ROOT);put(p,target)
            if p.name=='paper_repro_common.hpp':
                text=target.read_text();old='#include "../ANNProof-24FGCS/upstream/source/similarity_search/include/Keccak256.h"';assert old in text
                write(target,text.replace(old,'#include "Keccak256.h"'))
            if p.name=='work9_ablation.cpp':
                text=target.read_text();old='#include "../../build/multidata_lut_include.cpp"';assert old in text
                write(target,text.replace(old,'#include "../../build/qvann_protocol_include.cpp"'))
            provenance.append(dict(original=str(p.relative_to(ROOT)),packaged=str(target.relative_to(STAGE)),original_sha256=sha(p),packaged_sha256=sha(target)))
    core=ROOT/'build/veriann_lut_benchmark.cpp';code=core.read_text()
    substitutions={str(ROOT/'src/experiments/refinement_merkle_ordering.cpp'):'experiments/refinement_merkle_ordering.cpp',str(ROOT/'src/paper_repro_common.hpp'):'paper_repro_common.hpp',str(ROOT/'src/veriann_query_lut.hpp'):'veriann_query_lut.hpp'}
    for old,new in substitutions.items():assert old in code;code=code.replace(old,new)
    write(STAGE/'src/qvann_benchmark.cpp',code)
    provenance.append(dict(original='build/veriann_lut_benchmark.cpp',packaged='src/qvann_benchmark.cpp',original_sha256=sha(core),packaged_sha256=sha(STAGE/'src/qvann_benchmark.cpp'),edits='Only three absolute include paths made relative; algorithm unchanged'))
    for name in ['quantization_refinement_probe.cpp','quantization_raw_disclosure_probe.cpp']:
        p=ROOT/'scripts'/name;text=p.read_text().replace('"quantization_original_include.cpp"','"qvann_protocol_include.cpp"');target=STAGE/'benchmarks'/name;write(target,text)
        provenance.append(dict(original=str(p.relative_to(ROOT)),packaged=str(target.relative_to(STAGE)),original_sha256=sha(p),packaged_sha256=sha(target),edits='Only generated protocol include filename changed'))
    vendor=ROOT/'third_party/LearnedEarlyTer-20SIGMOD'
    for p in vendor.rglob('*'):
        if p.is_file() and (p.suffix in ['.h','.hpp','.cpp','.cc'] or p.name in ['LICENSE','README.md','Makefile','configure','configure.ac','makefile.inc.in','INSTALL.md']):put(p,STAGE/p.relative_to(ROOT))
    for p in [vendor/'libfaiss.so',vendor/'lib/lib_lightgbm.so']:
        put(p,STAGE/p.relative_to(ROOT))
    # Versioned runtime files suffice; avoid duplicating unversioned aliases.
    for name in ['libgcc_s.so.1','libgfortran.so.5','libopenblas.so.0','libquadmath.so.0','libstdc++.so.6']:
        p=ROOT/'third_party/runtime/lib'/name;put(p,STAGE/p.relative_to(ROOT))
    crypto=ROOT/'ANNProof-24FGCS/upstream/source/similarity_search'
    put(crypto/'include/Keccak256.h',STAGE/'third_party/keccak/Keccak256.h');put(crypto/'src/Keccak256.cc',STAGE/'third_party/keccak/Keccak256.cc')
    for ds in ['sift1m','nytimes256','glove100','deep1m']:
        p=ROOT/'dataset'/ds/'README.md'
        if p.exists():put(p,STAGE/'docs/datasets'/(ds+'.md'))
    write(STAGE/'scripts/build.sh',BUILD);(STAGE/'scripts/build.sh').chmod(0o755)
    write(STAGE/'scripts/run_benchmark.py',RUN);write(STAGE/'scripts/smoke_test.py',SMOKE)
    write(STAGE/'README.md',README)
    write(STAGE/'THIRD_PARTY_NOTICES.md','''# Third-party components

* `third_party/LearnedEarlyTer-20SIGMOD`: locally preserved modified Faiss/HNSW
  source and original matching libfaiss.so. Existing Facebook MIT license is
  retained in that directory. This fork also includes LightGBM headers and links
  the original lib_lightgbm.so used by the project. Preserve header notices.
* `third_party/keccak`: Keccak implementation copied from the local ANNProof
  dependency, with the existing Project Nayuki attribution and upstream links.
* `third_party/runtime/lib`: the matching Linux x86-64 C++/GCC/Fortran/OpenBLAS
  shared runtime files already supplied with the local research package.

This ZIP preserves dependencies for the user's local reproduction; it does not
relicense them or assign a license to QVANN. Review component licensing before
redistribution. No third-party baseline dataset or full paper repository is bundled.
''')
    write(STAGE/'.gitignore','/build/\n/cache/\n/results/\n/dataset/\n__pycache__/\n*.pyc\n.env\n.env.*\n*.pem\n*.key\n')
    write(STAGE/'PACKAGE_PROVENANCE.json',json.dumps(dict(date='2026-10-23',project='QVANN',source_edits=provenance,excluded=['datasets','index caches','papers','Excel workbooks','result archives','Git metadata','credentials','workbook result-adjustment scripts']),ensure_ascii=False,indent=2))
    print('PREPARED',STAGE,flush=True)

def eligible():
    return [p for p in sorted(STAGE.rglob('*')) if p.is_file() and not any(x in ['build','cache','results','__pycache__'] for x in p.relative_to(STAGE).parts)]

def archive():
    files=eligible();manifest=''.join(sha(p)+'  '+p.relative_to(STAGE).as_posix()+'\n' for p in files if p.name!='MANIFEST.sha256')
    write(STAGE/'MANIFEST.sha256',manifest)
    temp=ZIP.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temp,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in eligible():z.write(p,'QVANN/'+p.relative_to(STAGE).as_posix())
    with zipfile.ZipFile(temp) as z:assert z.testzip() is None
    temp.replace(ZIP);print('ZIP_READY',ZIP,ZIP.stat().st_size,flush=True)

def validate():
    assert ZIP.exists()
    with tempfile.TemporaryDirectory(prefix='QVANN unpack validation ') as temp:
        extracted=Path(temp)
        with zipfile.ZipFile(ZIP) as z:z.extractall(extracted)
        r=extracted/'QVANN'
        for line in (r/'MANIFEST.sha256').read_text().splitlines():
            expected,name=line.split('  ',1);assert sha(r/name)==expected,name
        print('VALIDATING_FRESH_EXTRACTION',r,flush=True)
        for action,cmd in [('build',['bash','scripts/build.sh']),('smoke',['python3','scripts/smoke_test.py'])]:
            with (STUDY/(action+'.log')).open('w') as f:
                p=subprocess.run(cmd,cwd=r,stdout=f,stderr=subprocess.STDOUT)
            if p.returncode:raise RuntimeError(action+' failed: '+(STUDY/(action+'.log')).read_text()[-3500:])
            print('VALIDATION_PASS',action,flush=True)
        report=dict(passed=True,date='2026-10-23',fresh_zip_extraction=True,path_with_spaces=True,compiled_targets=['qvann','quantization_probe','raw_probe','ablation'],smoke_protocol='2048 synthetic16D vectors,20 queries×3 repeats, real VO verification and tamper rejection; no external datasets or original build tree accessed')
        write(STAGE/'VALIDATION.json',json.dumps(report,indent=2));write(STUDY/'validation.json',json.dumps(report,indent=2))
    archive()
    summary=dict(zip=str(ZIP),bytes=ZIP.stat().st_size,MiB=round(ZIP.stat().st_size/2**20,2),sha256=sha(ZIP),files=len(eligible()),validation='passed: clean extraction build + synthetic proof/verification/tamper tests')
    write(STUDY/'PACKAGE_READY.json',json.dumps(summary,ensure_ascii=False,indent=2));write(ZIP.with_suffix('.zip.sha256'),summary['sha256']+'  '+ZIP.name+'\n')
    print(json.dumps(summary,ensure_ascii=False,indent=2),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('action',choices=['prepare','archive','validate']);a=p.parse_args();globals()[a.action]()
