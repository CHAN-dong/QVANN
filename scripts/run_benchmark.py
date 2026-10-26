#!/usr/bin/env python3
"""Portable single-thread benchmark launcher; no result scaling or prediction."""
import argparse,json,os,subprocess,time
from pathlib import Path
R=Path(__file__).resolve().parents[1]
PRESETS={'sift':(1000000,8,100,128),'nytimes':(289761,16,200,256),'glove':(1183514,16,200,1536),'deep':(1000000,16,200,64)}
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--dataset',type=Path,required=True,help='directory containing sift_base.fvecs and sift_query.fvecs')
 p.add_argument('--mode',choices=['qvann'],default='qvann')
 p.add_argument('--preset',choices=list(PRESETS));p.add_argument('--base',type=int);p.add_argument('--M',type=int);p.add_argument('--ef-construction',type=int);p.add_argument('--budget',type=int)
 p.add_argument('--queries',type=int,default=20);p.add_argument('--query-offset',type=int,default=100);p.add_argument('--repeats',type=int,default=3);p.add_argument('--k',type=int,default=10)
 p.add_argument('--cache',type=Path,default=R/'cache');p.add_argument('--output',type=Path);p.add_argument('--variants',default='1,2');p.add_argument('--nlist',type=int,default=256)
 a=p.parse_args();defaults=PRESETS.get(a.preset,(100000,16,100,64))
 n,m,ef,E=[value if value is not None else fallback for value,fallback in zip([a.base,a.M,a.ef_construction,a.budget],defaults)]
 for name in ['sift_base.fvecs','sift_query.fvecs']:
  if not (a.dataset/name).is_file():p.error('missing '+str(a.dataset/name))
 binary=R/'build'/'qvann'
 if not binary.is_file():p.error('run bash scripts/build.sh first')
 out=(a.output or R/'results'/(a.mode+'-'+time.strftime('%Y%m%d-%H%M%S'))).resolve()
 if out.exists() and any(out.iterdir()):p.error('output must be empty: '+str(out))
 out.mkdir(parents=True,exist_ok=True);a.cache.mkdir(parents=True,exist_ok=True)
 env=dict(os.environ)
 for key in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','RAYON_NUM_THREADS']:env[key]='1'
 env['LD_LIBRARY_PATH']=':'.join(map(str,[Path(os.environ['QVANN_DEPS_DIR'])/'runtime/lib',Path(os.environ['QVANN_DEPS_DIR'])/'LearnedEarlyTer-20SIGMOD',Path(os.environ['QVANN_DEPS_DIR'])/'LearnedEarlyTer-20SIGMOD/lib']))
 cmd=list(map(str,[binary,'--dataset',a.dataset.resolve(),'--base',n,'--M',m,'--ef-construction',ef,'--ef-search',E,'--queries',a.queries,'--query-offset',a.query_offset,'--repeats',a.repeats,'--k',a.k,'--cache',a.cache.resolve(),'--output',out,'--build-threads',1]))
 (out/'invocation.json').write_text(json.dumps(dict(command=cmd,threads=1,mode=a.mode,note='QVANN core only'),indent=2))
 subprocess.run(cmd,env=env,check=True)
 print('Results:',out)
if __name__=='__main__':main()
