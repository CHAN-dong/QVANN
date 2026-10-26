"""Finish serial measurements and write the workbook only after all pass."""
import argparse,os,subprocess,sys,time
from work9_index_study import ROOT,OUT,DATA,env
p=argparse.ArgumentParser();p.add_argument('--wait-pid',type=int);a=p.parse_args()
if a.wait_pid:
    print('Waiting for serial graph builds',a.wait_pid,flush=True)
    while True:
        try:os.kill(a.wait_pid,0)
        except ProcessLookupError:break
        time.sleep(2)
for ds in DATA:
    for method in ['ANNProof','Ours','VLAH']:
        assert (OUT/ds/method/'measurement.json').exists(),(ds,method,'missing graph build')
for ds in DATA:
    print('RUN V3DB',ds,flush=True)
    log=OUT/(ds+'-V3DB.log')
    with log.open('w') as f:
        subprocess.run([sys.executable,str(ROOT/'scripts/work9_index_v3db.py'),'--dataset',ds],env=env(),stdout=f,stderr=subprocess.STDOUT,check=True)
    print('DONE V3DB',ds,flush=True)
subprocess.run([sys.executable,str(ROOT/'scripts/fill_work9_index.py')],env=env(),check=True)
subprocess.run([sys.executable,str(ROOT/'scripts/audit_work9_index.py')],env=env(),check=True)
