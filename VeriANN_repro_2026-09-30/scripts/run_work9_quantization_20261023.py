"""Measured controlled refinement sweep; all baseline workbook cells preserved."""
import csv
import json
import math
import shutil
import statistics
from pathlib import Path

import work9_resource_matched as u

R = u.ROOT
S = R/'experiments/work9-quantization-20261023'
O = R/'results/work9-quantization-20261023'
S.mkdir(parents=True, exist_ok=True)
u.STUDY, u.RESULTS = S, O
BACKUP = S/'work9_before_quantization.xlsx'
if not BACKUP.exists():
    shutil.copy2(R.parent/'work9.xlsx', BACKUP)
DATA = {
    'sift1m': ('SIFT1M', 128, 'results/work9-ours-lut-20261015/sift1m-overall'),
    'nytimes256': ('NYTimes-256', 256, 'results/work9-nytimes-20261017/nytimes256/Ours-curve-289761'),
    'glove100': ('GloVe-100', 1536, 'results/work9-multidata-20261015/glove100/Ours-curve-1183514'),
    'deep1m': ('DEEP1M', 64, 'results/work9-multidata-20261015/deep1m/Ours-curve-1000000'),
}
MODES = ['SQ4--raw', 'SQ4--SQ6--raw', 'SQ4--SQ8--raw', 'SQ4--SQ6--SQ8--raw']
source = R/'scripts/quantization_refinement_probe.cpp'
original = R/'build/veriann_lut_benchmark.cpp'
prefix = original.read_text().split('int main(int argc,char**argv)try{')[0]
assert prefix.endswith('} // namespace optimized\n\n')
include = S/'quantization_original_include.cpp'
include.write_text(prefix)
binary = S/'quantization_probe'
if not binary.exists():
    cmd = u.read(R/'experiments/work9-ablation-20261015/build_command.json')
    cmd = [str(source) if str(x).endswith('/src/experiments/work9_ablation.cpp') else x for x in cmd]
    cmd[cmd.index('-o')+1] = str(binary)
    cmd.insert(1, '-I'+str(S))
    u.command(cmd, S/'build.log', timeout=300)
    u.write(S/'build.json', dict(command=cmd, source_sha256=u.sha(source), original_sha256=u.sha(original), include_sha256=u.sha(include), binary_sha256=u.sha(binary)))

configs = {}
for ds, (sheet, E, oldpath) in DATA.items():
    manifest = u.read(R/oldpath/'manifest.json')
    stem = manifest['dataset_sha256'][:16]
    name = f'{stem}_M{manifest["M"]}_efc{manifest["ef_construction"]}.index'
    cache = R/'ANNProof-24FGCS/cache'
    assert (cache/name).exists(), name
    configs[ds] = dict(sheet=sheet, E=E, source=str(R/oldpath), manifest=manifest, graph=str(cache/name), graph_sha256=u.sha(cache/name))
u.write(S/'protocol.json', dict(configurations=MODES, datasets=configs, k=10, queries=20, repeats=3, query_offset=100, threads=1,
    scope='Fix original SQ4 top-1 traversal/graph/E within each dataset; only refinement payload, nested bounds and corresponding Merkle proofs vary.',
    query='original SQ4 walk + exact ranking of disclosed candidates + actual serialized VO generation; includes query LUT preparation; no transfer',
    verify='VO-only decoding/authentication/SQ4 replay/progressive completeness and top-k checks; no server graph or raw arrays',
    refinement='nested uniform SQ4/6/8, incremental 2/4/2 bits with BFS Merkle ordering; raw uses original float32 coordinates',
    timing_order='rotate configurations by (query_index+repeat) modulo4; 60 samples/configuration; reset per-query LUT cache',
    baseline_check='SQ4-SQ6-raw packet bytes must exactly match original protocol for all60 samples/dataset',
    note='Recall@10 is measured as a control; no new recall tuning or performance coefficients; original workbook tables untouched'))

summary = []
for ds, cfg in configs.items():
    m = cfg['manifest']; out = O/ds
    if not (out/'PASS.txt').exists():
        cmd = [binary, '--dataset', R/'dataset'/ds, '--base', m['base'], '--M', m['M'], '--ef-construction', m['ef_construction'], '--cache', Path(cfg['graph']).parent, '--output', out, '--queries', 20, '--query-offset', 100, '--repeats', 3, '--build-threads', 1, '--k', 10, '--ef-search', cfg['E']]
        u.command(cmd, S/(ds+'.log'), timeout=3600, bounded=True)
        u.write(out/'environment.json', dict(command=list(map(str,cmd)), binary_sha256=u.sha(binary), graph_sha256=cfg['graph_sha256']))
    assert u.sha(cfg['graph']) == cfg['graph_sha256']
    identity = u.read(out/'identity.json')
    assert identity['dataset_sha256'] == m['dataset_sha256'] and identity['legacy_wire_checks'] == 60
    rows = list(csv.DictReader((out/'samples.csv').open()))
    assert len(rows) == 240
    truth = {int(r['query']): set(map(int,r['answer'].split())) for r in csv.DictReader((out/'ground_truth.csv').open())}
    old = [r for r in csv.DictReader((Path(cfg['source'])/'per_query.csv').open()) if int(r['k']) == 10 and int(r['ef_search']) == cfg['E'] and 100 <= int(r['query']) < 120]
    old_recall = {(int(r['query']),int(r['repeat'])):float(r['recall_at_k']) for r in old}
    assert len(old_recall) == 60
    for q in range(100,120):
        for rep in range(3):
            group = [r for r in rows if int(r['query'])==q and int(r['repeat'])==rep]
            assert len(group)==4 and len({r['trace_sha256'] for r in group})==len({r['answer'] for r in group})==1
    for mode in MODES:
        rr = [r for r in rows if r['configuration']==mode]
        assert len(rr)==60 and {(int(r['query']),int(r['repeat'])) for r in rr}=={(q,t) for q in range(100,120) for t in range(3)}
        for row in rr:
            ans = list(map(int,row['answer'].split())); q,rep = int(row['query']),int(row['repeat'])
            assert len(set(ans))==len(ans)==10 and row['valid']=='1'
            assert math.isclose(len(set(ans)&truth[q])/10,float(row['recall']),abs_tol=1e-12)
            assert math.isclose(float(row['recall']),old_recall[q,rep],abs_tol=1e-12)
            assert math.isclose(float(row['query_ms']),float(row['search_ms'])+float(row['vo_generation_ms']),rel_tol=1e-12)
        record = dict(dataset=ds,sheet=cfg['sheet'],configuration=mode,N=m['base'],D=identity['D'],M=m['M'],ef_construction=m['ef_construction'],E=cfg['E'],samples=60,independent_queries=20,
                      source=str(out/'samples.csv'),csv_sha256=u.sha(out/'samples.csv'),status='measured')
        for metric in ['recall','search_ms','vo_generation_ms','query_ms','verify_ms','vo_bytes','raw_vectors','stage1_records','stage2_records']:
            record[metric]=statistics.fmean(float(r[metric]) for r in rr)
            if metric.endswith('_ms'):record[metric+'_stdev']=statistics.stdev(float(r[metric]) for r in rr)
        record['vo_kb']=record['vo_bytes']/1000
        summary.append(record)
    attacks=list(csv.DictReader((out/'tamper.csv').open()))
    assert len(attacks)==identity['tamper_tests'] and all(r['rejected']=='1' for r in attacks)
    print('DATASET_COMPLETE',ds,[(r['configuration'],r['query_ms'],r['vo_kb'],r['verify_ms'],r['recall']) for r in summary if r['dataset']==ds],flush=True)
u.write(S/'summary.json',summary)
u.write(S/'FINISHED.json',dict(passed=True,datasets=4,configurations=16,queries_executed=960,legacy_wire_checks=240))
print('ALL_QUANTIZATION_RUNS_COMPLETE',flush=True)
