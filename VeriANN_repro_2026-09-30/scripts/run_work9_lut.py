"""Remeasure every Ours table point using the validated LUT implementation."""
import csv
from datetime import datetime
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from work9_lut_study import ROOT,STUDY,configure


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def event(**fields):
    fields['time']=datetime.now().isoformat()
    with (STUDY/'events.jsonl').open('a') as f:f.write(json.dumps(fields)+'\n')
    print(json.dumps(fields),flush=True)


def specifications():
    audit=json.loads((ROOT/'experiments/work9-ours-20261015/workbook_audit.json').read_text())
    cells={c['cell']:c for c in audit['cells']}
    def make(name,coordinates):
        points=[cells[c] for c in coordinates];folders={p['run'] for p in points};assert len(folders)==1
        folder=Path(points[0]['run']);m=json.loads((folder/'manifest.json').read_text())
        return {'name':name,'previous_output':str(folder),'base':m['base'],'queries':m['queries'],
                'offset':m['query_offset'],'repeats':m['repeats'],
                'k':','.join(map(str,sorted({p['k'] for p in points}))),
                'E':','.join(map(str,sorted({p['E'] for p in points}))),
                'dataset':str(ROOT/'dataset'/('sift1m' if name.startswith('sift1m') else 'bigann10m')),
                'selection_basis':'fixed_matched_workpoint' if 'matched-recall' in folder.name else 'prior_independent_calibration'}
    jobs=[make('sift1m-overall',['E11']),make('sift1m-recall-curve',[f'{c}23' for c in 'FGHIJKLMNOP']),
          make('sift1m-k-curve',[f'{c}86' for c in 'FGHIJK'])]
    jobs +=[make(f'size-{n}-evaluation',[f'{c}56']) for c,n in zip('FGHIJ',[100000,500000,1000000,5000000,10000000])]
    return jobs


def equivalent(spec,output):
    with (output/'per_query.csv').open() as f:new=list(csv.DictReader(f))
    ks=set(map(int,spec['k'].split(',')));Es=set(map(int,spec['E'].split(',')))
    previous=Path(spec['previous_output'])
    with (previous/'per_query.csv').open() as f:
        old=[r for r in csv.DictReader(f) if int(r['k']) in ks and int(r['ef_search']) in Es]
    assert len(new)==len(old)==len(ks)*len(Es)*spec['queries']*spec['repeats']
    keys=[key for key in old[0] if key!='method' and not key.endswith('_ms')]
    for a,b in zip(old,new):assert all(a[key]==b[key] for key in keys),(spec['name'],a['query'])
    assert (output/'trusted_roots.txt').read_bytes()==(previous/'trusted_roots.txt').read_bytes()
    # A multi-budget previous run may have saved query0.vo at a different E.
    if len(list(csv.DictReader((previous/'summary.csv').open())))==1:
        assert sha(output/'query0.vo')==sha(previous/'query0.vo')
    return {'status':'PASS','rows':len(new),'unchanged_non_timing_columns':keys,
            'previous_output':str(previous),'roots_identical':True}


def main():
    wb=configure();wb.initialize()
    proof=ROOT/'experiments/ours-lut-20261015/protocol_validation.json'
    assert json.loads(proof.read_text())['status']=='PASS'
    binary=ROOT/'build/veriann_lut_benchmark';source=ROOT/'build/veriann_lut_benchmark.cpp'
    previous=json.loads((ROOT/'results/ours-lut-20261015/lut-a/environment.json').read_text())
    assert sha(binary)==previous['binary_sha256']
    for path,expected in previous['source_sha256'].items():assert sha(ROOT/path)==expected
    shutil.copy2(proof,STUDY/'protocol_validation.json')
    jobs=specifications();(STUDY/'frozen_workpoints.json').write_text(json.dumps(jobs,indent=2)+'\n')
    for spec in jobs:
        name=spec['name'];record_file=STUDY/(name+'.json');output=ROOT/'results/work9-ours-lut-20261015'/name
        if record_file.exists():output=Path(json.loads(record_file.read_text())['output'])
        if not (output/'PASS.txt').exists():
            if output.exists() and any(output.iterdir()):output=output.with_name(name+'-retry-'+datetime.now().strftime('%H%M%S'))
            command=[sys.executable,str(ROOT/'scripts/run_ours_benchmark.py'),'--dataset',spec['dataset'],
                     '--output',str(output),'--base',str(spec['base']),'--queries',str(spec['queries']),
                     '--query-offset',str(spec['offset']),'--repeats',str(spec['repeats']),
                     '--k',spec['k'],'--E',spec['E'],'--source',str(source),'--binary',str(binary),'--skip-build']
            event(action='start',experiment=name,command=command)
            with (STUDY/(name+'.console.log')).open('w') as log:
                status=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            if status.returncode:
                event(action='failed',experiment=name)
                raise RuntimeError((STUDY/(name+'.console.log')).read_text()[-10000:])
        equality=equivalent(spec,output)
        (output/'scalar_equivalence.json').write_text(json.dumps(equality,indent=2)+'\n')
        with (output/'summary.csv').open() as f:summary=list(csv.DictReader(f))
        record={'method':'VeriANN-LUT','name':name,'dataset':spec['dataset'],'base':spec['base'],
                'queries':spec['queries'],'query_offset':spec['offset'],'repeats':spec['repeats'],
                'output':str(output),'summary':summary,'selection_basis':spec['selection_basis'],
                'previous_output':spec['previous_output']}
        record_file.write_text(json.dumps(record,indent=2)+'\n')
        event(action='passed',experiment=name,rows=equality['rows'])
        subprocess.run([sys.executable,str(ROOT/'scripts/fill_work9_lut.py')],cwd=ROOT,check=True)
    subprocess.run([sys.executable,str(ROOT/'scripts/fill_work9_lut.py'),'--update-original'],cwd=ROOT,check=True)


if __name__=='__main__':main()
