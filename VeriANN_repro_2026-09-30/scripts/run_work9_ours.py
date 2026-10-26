"""Sequential, resumable measurements for every Ours block of work9.xlsx."""
import csv
from datetime import datetime
import json
from pathlib import Path
import subprocess
import sys
from fill_work9_ours import ROOT,STUDY,initialize


def event(**fields):
    fields['time']=datetime.now().isoformat()
    with (STUDY/'events.jsonl').open('a') as f:f.write(json.dumps(fields)+'\n')
    print(json.dumps(fields),flush=True)


def run(name,dataset,n=1000000,k='10',E='64',queries=100,offset=100,repeats=3):
    record_file=STUDY/(name+'.json');output=ROOT/'results/work9-ours-20261015'/name
    if record_file.exists():
        record=json.loads(record_file.read_text());output=Path(record['output'])
    if not (output/'PASS.txt').exists():
        if output.exists() and any(output.iterdir()):output=output.with_name(name+'-retry-'+datetime.now().strftime('%H%M%S'))
        command=[sys.executable,str(ROOT/'scripts/run_ours_benchmark.py'),'--dataset',str(dataset),
                 '--output',str(output),'--base',str(n),'--k',str(k),'--E',str(E),
                 '--queries',str(queries),'--query-offset',str(offset),'--repeats',str(repeats),'--skip-build']
        event(action='start',experiment=name,command=command)
        with (STUDY/(name+'.console.log')).open('w') as log:
            status=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        if status.returncode:
            event(action='failed',experiment=name)
            raise RuntimeError((STUDY/(name+'.console.log')).read_text()[-10000:])
        event(action='passed',experiment=name)
    else:event(action='resume_passed',experiment=name)
    with (output/'summary.csv').open() as f:summary=list(csv.DictReader(f))
    record={'method':'VeriANN-optimized','name':name,'dataset':str(dataset),'base':n,'queries':queries,
            'query_offset':offset,'repeats':repeats,'output':str(output),'summary':summary}
    record_file.write_text(json.dumps(record,indent=2)+'\n')
    subprocess.run([sys.executable,str(ROOT/'scripts/fill_work9_ours.py')],cwd=ROOT,check=True)
    return record


def choose(record):
    eligible=[s for s in record['summary'] if float(s['recall_at_k'])>=.95]
    if not eligible:raise RuntimeError('calibration did not reach 95%; extend E sweep before evaluation')
    return int(min(eligible,key=lambda s:int(s['ef_search']))['ef_search'])


def main():
    initialize()
    Es='8,16,24,32,48,64,96,128,192,256,384,512,768'
    dataset=ROOT/'dataset/sift1m'
    calibration=run('sift1m-calibration',dataset,E=Es,offset=0,repeats=1)
    E=choose(calibration)
    run('sift1m-overall',dataset,E=str(E))
    run('sift1m-recall-curve',dataset,E='4,8,12,16,24,32,48,64,96,128,192')
    run('sift1m-k-curve',dataset,k='1,5,10,20,50,100',E=str(E))
    dataset=ROOT/'dataset/bigann10m'
    for n in [100000,500000,1000000,5000000,10000000]:
        calibration=run(f'size-{n}-calibration',dataset,n=n,E=Es,offset=0,repeats=1)
        E=choose(calibration)
        run(f'size-{n}-evaluation',dataset,n=n,E=str(E))
    subprocess.run([sys.executable,str(ROOT/'scripts/fill_work9_ours.py'),'--update-original'],cwd=ROOT,check=True)


if __name__=='__main__':main()
