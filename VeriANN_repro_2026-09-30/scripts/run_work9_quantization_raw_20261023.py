"""Measure missing Raw disclosure controls without changing SQ4 traversal."""
import csv
import math
import shutil
import statistics
from pathlib import Path
import work9_resource_matched as u

R=u.ROOT;Q=R/'experiments/work9-quantization-20261023';S=R/'experiments/work9-quantization-raw-20261023';O=R/'results/work9-quantization-raw-20261023'
S.mkdir(parents=True,exist_ok=True);u.STUDY,u.RESULTS=S,O
BACKUP=S/'work9_before_raw_quantization.xlsx'
if not BACKUP.exists():shutil.copy2(R.parent/'work9.xlsx',BACKUP)
proto=u.read(Q/'protocol.json');source=R/'scripts/quantization_raw_disclosure_probe.cpp';binary=S/'raw_probe'
if not binary.exists():
    cmd=u.read(Q/'build.json')['command']
    cmd=[str(source) if str(x).endswith('/scripts/quantization_refinement_probe.cpp') else x for x in cmd]
    cmd[cmd.index('-o')+1]=str(binary)
    u.command(cmd,S/'build.log',timeout=300)
    u.write(S/'build.json',dict(command=cmd,source_sha256=u.sha(source),original_include_sha256=u.sha(Q/'quantization_original_include.cpp'),binary_sha256=u.sha(binary)))
u.write(S/'protocol.json',dict(datasets=proto['datasets'],queries=20,repeats=3,query_offset=100,k=10,threads=1,
    raw='Unquantized disclosure control under fixed SQ4 top-1 traversal, not a raw-distance traversal baseline. Client reconstructs original SQ4 codes from authenticated raw float32 vectors using unchanged trusted quantization metadata.',
    authentication='Unchanged expansion-centric direct-content strategy and canonical shared multiproof; expansion leaf commits neighbor IDs and raw float32 coordinates rather than SQ4 codes. Entry raw vector is part of the offline trusted anchor.',
    timing='Q includes fixed SQ4 walk, exact candidate ranking and actual raw VO generation. V includes decode, reconstruct SQ4 codes, Merkle authentication, deterministic SQ4 replay and exact top-k check among disclosed vectors.',
    controls='Original graph/data hashes,E,query IDs,trace hash,ordered answers and recall must match all four prior refinement configurations. This is a new raw wire encoding, not an estimate of old HNSW-ADS.',
    previous='Four non-Raw rows reuse the complete original measured quantization runs; no new coefficients or forecasts.'))
summary=[]
for ds,cfg in proto['datasets'].items():
    out=O/ds;m=cfg['manifest']
    assert u.sha(cfg['graph'])==cfg['graph_sha256']
    if not (out/'PASS.txt').exists():
        cmd=[binary,'--dataset',R/'dataset'/ds,'--base',m['base'],'--M',m['M'],'--ef-construction',m['ef_construction'],'--cache',Path(cfg['graph']).parent,'--output',out,'--queries',20,'--query-offset',100,'--repeats',3,'--build-threads',1,'--k',10,'--ef-search',cfg['E']]
        u.command(cmd,S/(ds+'.log'),timeout=3600,bounded=True)
        u.write(out/'environment.json',dict(command=list(map(str,cmd)),binary_sha256=u.sha(binary),graph_sha256=cfg['graph_sha256']))
    assert u.sha(cfg['graph'])==cfg['graph_sha256']
    identity=u.read(out/'identity.json');assert identity['dataset_sha256']==m['dataset_sha256']
    rows=list(csv.DictReader((out/'samples.csv').open()));assert len(rows)==60
    reference={(int(r['query']),int(r['repeat'])):r for r in csv.DictReader((R/'results/work9-quantization-20261023'/ds/'samples.csv').open()) if r['configuration']=='SQ4--SQ6--raw'}
    assert {(int(r['query']),int(r['repeat'])) for r in rows}==set(reference)
    for r in rows:
        ref=reference[int(r['query']),int(r['repeat'])]
        assert r['trace_sha256']==ref['trace_sha256'] and r['answer']==ref['answer']
        assert r['disclosed']==ref['disclosed'] and r['expanded']==ref['expanded'] and r['valid']=='1'
        assert math.isclose(float(r['recall']),float(ref['recall']),abs_tol=1e-12)
        assert int(r['record_bytes'])+int(r['proof_bytes'])==int(r['vo_bytes'])
    attacks=list(csv.DictReader((out/'tamper.csv').open()));assert len(attacks)==12 and all(r['rejected']=='1' for r in attacks)
    rec=dict(dataset=ds,sheet=cfg['sheet'],configuration='Raw',N=m['base'],D=identity['D'],M=m['M'],ef_construction=m['ef_construction'],E=cfg['E'],samples=60,independent_queries=20,source=str(out/'samples.csv'),csv_sha256=u.sha(out/'samples.csv'),status='measured: raw disclosure with fixed SQ4 top-1 traversal')
    for metric in ['recall','query_ms','verify_ms','vo_bytes']:
        rec[metric]=statistics.fmean(float(r[metric]) for r in rows)
        if metric.endswith('_ms'):rec[metric+'_stdev']=statistics.stdev(float(r[metric]) for r in rows)
    rec['vo_kb']=rec['vo_bytes']/1000;summary.append(rec)
    print('RAW_COMPLETE',ds,rec['vo_kb'],rec['verify_ms'],rec['recall'],flush=True)
u.write(S/'summary.json',summary)
u.write(S/'FINISHED.json',dict(passed=True,fresh_raw_samples=240,matched_traces_and_answers=240,rejected_tamper_cases=48))
print('ALL_RAW_CONFIGURATIONS_COMPLETE',flush=True)
