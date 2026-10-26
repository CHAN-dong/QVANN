#!/usr/bin/env python3
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
  for mode in ['qvann']:
   out=root/mode;cmd=[sys.executable,str(R/'scripts/run_benchmark.py'),*common,'--mode',mode,'--output',str(out)]
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
   assert len(groups)==60
   report[mode]=dict(verified_samples=len(groups),rejected_tamper_cases=rejected)
  print(json.dumps(dict(passed=True,tests=report),indent=2))
if __name__=='__main__':main()
