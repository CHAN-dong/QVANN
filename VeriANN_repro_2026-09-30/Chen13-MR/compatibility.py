"""Run after all timed benchmarks; check shared float32 ground truth agreement."""
import csv,json,statistics,subprocess
from run import HERE,OUT,STUDY,write,limits,env
assert (STUDY/'RUN_FINISHED.json').exists()
subprocess.run(['g++','-O3','-DNDEBUG','-std=c++17',str(HERE/'compatibility.cpp'),'-lcrypto','-o',str(HERE/'compatibility')],check=True)
records=[]
for f in sorted(OUT.glob('*/measurement.json')):
 r=json.loads(f.read_text())
 if r['dataset'].startswith('smoke') or r['kind']=='pilot':continue
 out=f.parent/'ground-truth-compatibility.csv'
 if not out.exists():
  cmd=[str(HERE/'compatibility'),r['command'][1],r['command'][2],str(r['N']),','.join(str(p['k']) for p in r['points']),str(out)]
  subprocess.run(cmd,check=True,preexec_fn=limits,env=env())
 with out.open() as stream:rows=list(csv.DictReader(stream))
 assert len(rows)==20*len(r['points'])
 points=[]
 for p in r['points']:
  selected=[x for x in rows if int(x['k'])==p['k']]
  recall=statistics.mean(float(x['shared_float32_recall']) for x in selected)
  points.append(dict(k=p['k'],shared_float32_recall=recall,exact_float64_recall=1,queries=20))
 records.append(dict(dataset=r['dataset'],N=r['N'],kind=r['kind'],points=points,source=str(out)))
 print('GT CHECK',f.parent.name,[(p['k'],p['shared_float32_recall']) for p in points],flush=True)
write(STUDY/'ground-truth-compatibility.json',dict(records=records,all_shared_recalls_one=all(p['shared_float32_recall']==1 for r in records for p in r['points'])))
