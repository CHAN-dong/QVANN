"""Build only the standalone controlled ablation executable (under WSL)."""
import json
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
STUDY=ROOT/'experiments/work9-ablation-20261015'
STUDY.mkdir(parents=True,exist_ok=True)
meta=json.loads((ROOT/'ANNProof-24FGCS/results/work9-20261015/sift1m-recall-curve/environment.json').read_text())
command=meta['build_command']
command=[str(ROOT/'src/experiments/work9_ablation.cpp') if x.endswith('/ANNProof-24FGCS/annproof.cpp') else str(ROOT/'build/work9_ablation') if x.endswith('/ANNProof-24FGCS/build/annproof') else x for x in command]
env=os.environ.copy()
env['LD_LIBRARY_PATH']=':'.join(map(str,[ROOT/'third_party/runtime/lib',ROOT/'third_party/LearnedEarlyTer-20SIGMOD',ROOT/'third_party/LearnedEarlyTer-20SIGMOD/lib']))
with (STUDY/'build.log').open('w') as f:
    result=subprocess.run(command,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
(STUDY/'build_command.json').write_text(json.dumps(command,indent=2)+'\n')
if result.returncode:
    print((STUDY/'build.log').read_text()[-18000:])
raise SystemExit(result.returncode)
