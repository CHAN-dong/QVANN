"""Measured-only, held-out routing Merkle layout experiment. Run in WSL."""
import argparse
import csv
import random
import re
import statistics as st
import struct
from pathlib import Path
import numpy as np
import work9_resource_matched as u

R=u.ROOT
SOURCE=R/'src/experiments/proof_aware_layout.cpp'
SEED=20261024
NTRAIN=256
NTEST=100
REPEATS=3
LAYOUTS=['ID','Random','BFS','Proof-Aware']

def writecsv(path,rows):
    with path.open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)

def build():
    original=R/'build/veriann_lut_benchmark.cpp';text=original.read_text()
    prefix=text.split('int main(int argc,char**argv)try{')[0]
    generator=prefix.split('Packet generate(')[1].split('// The verifier receives')[0]
    generator='Packet generate_layout('+generator
    generator=generator.replace('const float*q,int k,int E){','const float*q,int k,int E,const std::vector<Id>&route_position,const p::Tree&routing_tree){')
    generator=generator.replace('Id pos=ads.route_base[id]+l;','Id pos=route_position.at(ads.route_base[id]+l);')
    generator=generator.replace('proof_write(b,*ads.route,route_targets);','proof_write(b,routing_tree,route_targets);')
    assert 'route_position.at' in generator and 'routing_tree,route_targets' in generator
    if B.exists():
        old=u.read(S/'build.json');assert old['source_sha256']==u.sha(SOURCE) and old['original_sha256']==u.sha(original),'Use a new --run-id after changing source'
        return
    (S/'layout_original_include.cpp').write_text(prefix)
    (S/'layout_packet_generator.cpp').write_text(generator)
    command=u.read(R/'experiments/work9-quantization-20261023/build.json')['command']
    command=[str(SOURCE) if str(x).endswith('/scripts/quantization_refinement_probe.cpp') else '-I'+str(S) if str(x).startswith('-I') and str(x).endswith('/experiments/work9-quantization-20261023') else x for x in command]
    command[command.index('-o')+1]=str(B)
    u.command(command,S/'build.log',timeout=300)
    u.write(S/'build.json',dict(command=command,source_sha256=u.sha(SOURCE),original_sha256=u.sha(original),binary_sha256=u.sha(B),include_sha256=u.sha(S/'layout_original_include.cpp'),serializer_sha256=u.sha(S/'layout_packet_generator.cpp')))

def run():
    configs=u.read(R/'experiments/work9-quantization-20261023/protocol.json')['datasets']
    data={ds:configs[ds] for ds in ['sift1m','deep1m']}
    protocol=dict(seed=SEED,train_queries=NTRAIN,test_queries=NTEST,repeats=REPEATS,threads=1,k=10,datasets=data,layouts=LAYOUTS,
        refinement='Native SQ4--SQ6--raw, unchanged shared BFS delta/raw trees',
        actual_default='Native Routing Tree is ID-major, NOT BFS. Only native refinement trees are BFS. This experiment additionally applies node-BFS order to the routing tree.',
        split='Fixed random shuffle of all original query IDs, first256 train and next100 held-out test. Split recorded before traces. Test traces and exact ground truth computed only after layout freeze.',
        optimizer='BFS initialization; four fixed block sweeps of256,2048,16384,2048 expansion-record leaves. Sort by MinHash of TRAIN query membership; exact true sibling-count improvement required to accept each block. Inactive records keep relative BFS order. No test-based algorithm/configuration selection.',
        random='std::mt19937(seed), std::shuffle of nodes, per-node records in increasing layer order',
        costs='Routing proof=4-byte hash-count +32bytes per sibling. Existing4-byte positions per expansion record remain present in ALL layouts and counted in total VO. Total authentication VO=all three serialized multiproofs. Also report positions+proofs; raw/quantized payloads counted once. KiB=1024bytes.',
        timing='Single thread steady_clock, includes VO decoding, all authentication, SQ4 traversal replay and exact final-result verification. Clear client LUT each trial; one untimed warmup for each query/layout. Rotate layout timing order by query and repetition.',
        construction='Tree construction includes reordering unchanged leaf hashes and hashing. Common codec+native ADS construction measured separately, not summed twice. Graph cache loading is not graph construction. Training collection and optimization measured separately. Non-ID serving mapping=4bytes per route record; optimizer temporary state not needed online.',
        limitations='One fixed training split/seed and256 training queries; stability across seeds or query-distribution shift is not established. No coefficient scaling, smoothing, prediction, or workbook edits.')
    u.write(S/'protocol.json',protocol)
    for ds,cfg in data.items():
        d=S/ds;d.mkdir(exist_ok=True);out=O/ds;m=cfg['manifest']
        qpath=R/'dataset'/ds/'sift_query.fvecs'
        with qpath.open('rb') as f:dimension=struct.unpack('<I',f.read(4))[0]
        assert qpath.stat().st_size%(4*(dimension+1))==0
        count=qpath.stat().st_size//(4*(dimension+1));assert count>=NTRAIN+NTEST
        ids=list(range(count));random.Random(SEED).shuffle(ids);train=ids[:NTRAIN];test=ids[NTRAIN:NTRAIN+NTEST]
        split=dict(seed=SEED,query_file=str(qpath),query_file_sha256=u.sha(qpath),query_count=count,train=train,test=test)
        if (d/'split.json').exists():assert u.read(d/'split.json')==split
        u.write(d/'split.json',split)
        for name,v in [('train',train),('test',test)]:(d/(name+'.ids')).write_text('\n'.join(map(str,v))+'\n')
        assert u.sha(cfg['graph'])==cfg['graph_sha256'],'Existing graph changed'
        if (out/'PASS.txt').exists():assert u.read(out/'environment.json')['binary_sha256']==u.sha(B);continue
        command=[B,'--dataset',R/'dataset'/ds,'--base',m['base'],'--M',m['M'],'--ef-construction',m['ef_construction'],'--cache',Path(cfg['graph']).parent,'--output',out,'--queries',NTEST,'--repeats',REPEATS,'--build-threads',1,'--k',10,'--ef-search',cfg['E'],'--train-ids',d/'train.ids','--test-ids',d/'test.ids']
        u.command(command,S/(ds+'.log'),bounded=True,timeout=7200)
        assert u.sha(cfg['graph'])==cfg['graph_sha256']
        u.write(out/'environment.json',dict(command=list(map(str,command)),binary_sha256=u.sha(B),graph_sha256=cfg['graph_sha256'],split_sha256=u.sha(d/'split.json')))

def report():
    summary=[];allrows=[];reductions=[];construction=[];query_changes=[]
    for ds,label in [('sift1m','SIFT'),('deep1m','DEEP')]:
        out=O/ds;assert (out/'PASS.txt').exists();split=u.read(S/ds/'split.json')
        rows=list(csv.DictReader((out/'samples.csv').open()));assert len(rows)==NTEST*REPEATS*4
        expected={(q,r) for q in split['test'] for r in range(REPEATS)}
        for row in rows:assert row['valid']=='1'
        for q,r in expected:
            group=[x for x in rows if int(x['query'])==q and int(x['repeat'])==r];assert len(group)==4
            for field in ['E','hits','recall','candidates','expanded','targets','payload_bytes','refined','raw','trace_sha256','refinement_sha256','answer']:
                assert len({x[field] for x in group})==1,(ds,q,r,field)
            assert len({int(x['total_bytes'])-int(x['route_proof_bytes']) for x in group})==1
            assert len({int(x['auth_proof_bytes'])-int(x['route_proof_bytes']) for x in group})==1
        for name in LAYOUTS:
            rr=[r for r in rows if r['layout']==name];assert {(int(x['query']),int(x['repeat'])) for x in rr}==expected
            mean=lambda key:st.fmean(float(r[key]) for r in rr)
            rec=dict(dataset=ds,Dataset=label,Layout=name,E=int(rr[0]['E']),R_pct=mean('recall')*100,C=mean('candidates'),Targets=mean('targets'),Sibling_Hashes=mean('sibling_hashes'),Routing_Proof_KiB=mean('route_proof_bytes')/1024,Authentication_Proof_KiB=mean('auth_proof_bytes')/1024,Route_Position_KiB=mean('route_position_bytes')/1024,Authentication_With_Route_Positions_KiB=(mean('auth_proof_bytes')+mean('route_position_bytes'))/1024,Total_VO_KiB=mean('total_bytes')/1024,V_ms=mean('verify_ms'),V_stdev_ms=st.stdev(float(r['verify_ms']) for r in rr),Auth_ms=mean('auth_ms'),Replay_and_Result_ms=mean('replay_and_result_ms'),independent_queries=NTEST,repeats=REPEATS,source=str(out/'samples.csv'),source_sha256=u.sha(out/'samples.csv'))
            summary.append(rec)
        costs=list(csv.DictReader((out/'proof_costs.csv').open()))
        a=next(r for r in summary if r['dataset']==ds and r['Layout']=='BFS');b=next(r for r in summary if r['dataset']==ds and r['Layout']=='Proof-Aware')
        reduction=dict(dataset=ds)
        for key in ['Sibling_Hashes','Routing_Proof_KiB','Total_VO_KiB','V_ms']:reduction[key+'_reduction_pct']=(1-b[key]/a[key])*100
        for part in ['train','test']:
            points={name:{int(x['query']):int(x['sibling_hashes']) for x in costs if x['layout']==name and x['split']==part} for name in ['BFS','Proof-Aware']}
            aa,bb=points['BFS'],points['Proof-Aware'];assert set(aa)==set(bb)
            reduction[part+'_mean_hashes_BFS']=st.fmean(aa.values());reduction[part+'_mean_hashes_aware']=st.fmean(bb.values())
            reduction[part+'_hashes_reduction_pct']=(1-sum(bb.values())/sum(aa.values()))*100
            reduction[part+'_wins']=sum(bb[q]<aa[q] for q in aa);reduction[part+'_losses']=sum(bb[q]>aa[q] for q in aa);reduction[part+'_ties']=sum(bb[q]==aa[q] for q in aa)
            changes=[(1-bb[q]/aa[q])*100 for q in aa];reduction[part+'_worst_reduction_pct']=min(changes);reduction[part+'_best_reduction_pct']=max(changes);reduction[part+'_median_reduction_pct']=st.median(changes)
            for q in aa:query_changes.append(dict(dataset=ds,split=part,query=q,BFS_siblings=aa[q],Proof_Aware_siblings=bb[q],hash_reduction_pct=(1-bb[q]/aa[q])*100))
        pairs={name:{q:st.fmean(float(x['verify_ms']) for x in rows if x['layout']==name and int(x['query'])==q) for q in split['test']} for name in ['BFS','Proof-Aware']}
        reduction['test_verification_query_wins']=sum(pairs['Proof-Aware'][q]<pairs['BFS'][q] for q in split['test']);reduction['test_verification_query_losses']=NTEST-reduction['test_verification_query_wins']
        # Resample independent query PAIRS, not the300 repeated measurements.
        vb=np.array([pairs['BFS'][q] for q in split['test']]);vp=np.array([pairs['Proof-Aware'][q] for q in split['test']]);rng=np.random.default_rng(SEED);indices=rng.integers(0,NTEST,size=(10000,NTEST));bootstrap=(1-vp[indices].mean(axis=1)/vb[indices].mean(axis=1))*100
        reduction['V_reduction_paired_bootstrap_95_low_pct'],reduction['V_reduction_paired_bootstrap_95_high_pct']=map(float,np.quantile(bootstrap,[.025,.975]))
        for other in ['ID','Random']:
            ref=next(x for x in summary if x['dataset']==ds and x['Layout']==other);reduction['BFS_hash_reduction_vs_'+other+'_pct']=(1-a['Sibling_Hashes']/ref['Sibling_Hashes'])*100
        cr=list(csv.DictReader((out/'construction.csv').open()));construction.extend(dict(dataset=ds,**x) for x in cr)
        ca=next(x for x in cr if x['layout']=='BFS');cb=next(x for x in cr if x['layout']=='Proof-Aware')
        extra=float(cb['optimization_ms'])+float(cb['tree_build_ms'])-float(ca['tree_build_ms']);reduction['extra_layout_plus_tree_ms']=extra;reduction['training_trace_ms']=float(cb['train_trace_ms']);reduction['extra_serving_mapping_bytes_vs_BFS']=int(cb['mapping_bytes'])-int(ca['mapping_bytes']);saving=a['V_ms']-b['V_ms'];reduction['client_time_break_even_queries']=extra/saving if saving>0 else 'no observed saving';reductions.append(reduction)
        allrows.extend(dict(dataset=ds,**r) for r in rows)
    for name,rr in [('summary',summary),('samples_all',allrows),('reductions',reductions),('construction',construction),('per_query_changes',query_changes)]:writecsv(S/(name+'.csv'),rr);u.write(S/(name+'.json'),rr)
    lines=[r'\begin{table}[t]',r'\centering',r'\caption{Impact of routing Merkle leaf layout ($k=10$). Identical held-out queries and expansion/refinement records across layouts. RP: routing multiproof (KiB); VO: total VO (KiB); $V$: full verification time (ms).}',r'\label{tab:proof_aware_layout}',r'\footnotesize',r'\setlength{\tabcolsep}{2pt}',r'\resizebox{\columnwidth}{!}{%',r'\begin{tabular}{@{}llrrrrr@{}}',r'\toprule',r'Dataset & Layout & Targets & Siblings & RP & VO & $V$ \\',r'\midrule']
    for i,x in enumerate(summary):
        if i==4:lines.append(r'\midrule')
        lines.append(' & '.join([x['Dataset'] if i%4==0 else '',x['Layout']]+[f'{x[k]:.2f}' for k in ['Targets','Sibling_Hashes','Routing_Proof_KiB','Total_VO_KiB','V_ms']])+r' \\')
    lines.extend([r'\bottomrule',r'\end{tabular}%',r'}',r'\end{table}','']);(S/'proof_aware_layout.tex').write_text('\n'.join(lines))
    text=['# Proof-Cost-Aware Routing Merkle Layout: measured held-out experiment','',
      '**Implementation correction:** the existing QVANN routing tree is ID-major; only refinement trees are BFS. ID is therefore the actual original-routing control, and BFS is a newly applied routing layout. The experiment does not claim that routing BFS was previously implemented.','',
      'Each dataset: original million-vector graph, k10, native SQ4–SQ6–raw, SIFT E128 / DEEP E64; fixedseed20261024 split of original query files into256 training and100 independent test queries. Three timed repetitions, plus one untimed warmup per query/layout. Layouts frozen before any test traversal or test ground truth. No parameter was selected using test results. Native verifier receives only VO, trusted roots, query and codec metadata.','',
      'ID/Random/BFS keep each node’s per-level expansion records adjacent; Proof-Aware may rearrange individual records within bounded blocks. Targets are opened (node,level) expansion records, including upper layers, rather than just unique expanded node IDs. Four predetermined MinHash membership block sweeps propose arrangements; every acceptance must strictly decrease actual training multiproof sibling count. The fast objective is checked against Tree::prove for all train/layout pairs; all test queries generate and verify genuine multiproofs.','',
      'Routing proof bytes=4+32×sibling hashes. Every layout transmits the same existing4-byte leaf position per target; metadata is included in Total VO. Total authentication proof bytes include all three count-prefixed proofs; the CSV additionally reports authentication proofs+route position bytes. Identical nonrouting payload length and identical refinement proof bytes are asserted for every query. Total VO differences are therefore exactly routing proof differences.','']
    for ds in ['sift1m','deep1m']:
        rr=[x for x in summary if x['dataset']==ds];bfs=rr[2];a=rr[3];d=next(x for x in reductions if x['dataset']==ds)
        text.extend([f"## {bfs['Dataset']}",'',f"Recall is identical across all four layouts: {bfs['R_pct']:.2f}%; candidates {bfs['C']:.2f}; targets {bfs['Targets']:.2f}. BFS→Proof-Aware: siblings {bfs['Sibling_Hashes']:.2f}→{a['Sibling_Hashes']:.2f}, route proof {bfs['Routing_Proof_KiB']:.2f}→{a['Routing_Proof_KiB']:.2f}KiB, total VO {bfs['Total_VO_KiB']:.2f}→{a['Total_VO_KiB']:.2f}KiB, verification {bfs['V_ms']:.2f}→{a['V_ms']:.2f}ms.",'',f"Reductions: sibling hashes {d['Sibling_Hashes_reduction_pct']:.2f}%; routing proof {d['Routing_Proof_KiB_reduction_pct']:.2f}%; total VO {d['Total_VO_KiB_reduction_pct']:.2f}%; verification time {d['V_ms_reduction_pct']:.2f}% (negative means regression). Training hash reduction {d['train_hashes_reduction_pct']:.2f}% versus held-out {d['test_hashes_reduction_pct']:.2f}%. Held-out query wins/losses/ties in proof hashes: {d['test_wins']}/{d['test_losses']}/{d['test_ties']}; median {d['test_median_reduction_pct']:.2f}%, worst {d['test_worst_reduction_pct']:.2f}%, best {d['test_best_reduction_pct']:.2f}%. Verification query wins/losses after averaging three repeats: {d['test_verification_query_wins']}/{d['test_verification_query_losses']}.",'',f"Extra optimization+route-tree construction versus BFS {d['extra_layout_plus_tree_ms']/1000:.2f}s; training trace collection {d['training_trace_ms']/1000:.2f}s separately. Serving mapping overhead versus BFS: {d['extra_serving_mapping_bytes_vs_BFS']} bytes. Illustrative client CPU break-even {d['client_time_break_even_queries']} queries, excluding network/trace cost; not an end-to-end deployment cost model.",''])
        cr=[x for x in construction if x['dataset']==ds];bc=next(x for x in cr if x['layout']=='BFS');pc=next(x for x in cr if x['layout']=='Proof-Aware')
        text.extend([f"BFS already cuts sibling hashes by {d['BFS_hash_reduction_vs_ID_pct']:.2f}% versus ID and {d['BFS_hash_reduction_vs_Random_pct']:.2f}% versus Random on independent queries. This supports useful graph-induced locality. Full verification reduction paired-query bootstrap95% interval: [{d['V_reduction_paired_bootstrap_95_low_pct']:.2f}%, {d['V_reduction_paired_bootstrap_95_high_pct']:.2f}%],10000 resamples of100 query pairs. This interval is conditional on this run and does not cover machine-to-machine or run-to-run systematic variation.",'',f"All four routing trees contain {int(pc['route_leaves']):,} real expansion-record leaves and occupy {int(pc['route_tree_bytes'])/2**20:.2f}MiB of SHA256 nodes (same padded tree shape). BFS and Proof-Aware each need {int(pc['mapping_bytes'])/2**20:.2f}MiB for the serving record-ordinal→leaf-position map, hence0 extra index bytes versus BFS. Reused BFS initialization is {float(bc['ordering_ms'])/1000:.2f}s, actual tree construction BFS {float(bc['tree_build_ms'])/1000:.2f}s / Proof-Aware {float(pc['tree_build_ms'])/1000:.2f}s; optimization alone {float(pc['optimization_ms'])/1000:.2f}s. Proof-Aware ordering_ms=0 in construction.csv denotes reuse of the already constructed BFS seed, NOT zero standalone BFS construction cost. The small seed-vector copy/inversion is outside optimization_ms; this is not an exhaustive end-to-end fresh-index timer.",''])
    reliable=all(d['Sibling_Hashes_reduction_pct']>0 and d['Total_VO_KiB_reduction_pct']>=5 and d['V_ms_reduction_pct']>=5 and d['test_wins']>d['test_losses'] for d in reductions)
    verdict=('Both datasets show material held-out gains in this fixed split, but multi-seed/generalization validation remains necessary before claiming a new contribution.' if reliable else 'The evidence does NOT support presenting this lightweight Proof-Aware layout as a strong, stable new QVANN contribution: assess the reported limited total-VO/time benefits and held-out regressions rather than training-objective gains. Keep it as an exploratory ablation; do not replace the default on this evidence.')
    text += ['## Judgment','',verdict,'','BFS locality is assessed against both ID and fixed-seed random in summary.csv. Any hash reduction is entirely explained by changed shared-path locality, not fewer records or changed recall. Verification includes unaffected record hashing, refinement verification and traversal replay; proof-byte gains do not imply proportional full verification speedup. Timing effects are descriptive and can include cache/OS noise despite rotated order and warmups. Three repeats are not three independent queries. This is one fixed held-out split, not a multiple-seed stability result.','',
      'Construction CSV separates common codec/native-ADS construction, cached graph loading, each actual routing tree construction and layout optimization. Common ADS construction already contains the original ID route tree; do not sum it again to claim full fresh index construction. Route leaves/digests and tree shape are identical-sized; non-ID serving map uses4 bytes per route record. Optimization traces/membership and inverse order can be discarded online. Mapping files saved for auditing include ID too, although ID positions can be implicit.','',
      'Reproduce (from inner project): `wsl -e /root/.venvs/v3db-work9/bin/python scripts/run_proof_aware_layout_20261024.py all --run-id proof-aware-layout-20261024-rerun`. Original sources, graph caches and workbook are not modified. New files: src/experiments/proof_aware_layout.cpp and scripts/run_proof_aware_layout_20261024.py; a generated serializer changes only routing-position lookup and proof tree. build.json contains the exact compiler command and source/binary hashes. protocol.json and per-dataset split.json provide every parameter and query ID. Raw samples, training/test traces, costs, optimizer iteration history, full position permutations, sample VOs and roots are under results/<run-id>/.','']
    (S/'REPORT.md').write_text('\n'.join(text),encoding='utf-8')
    paper=[r'\paragraph{Proof-cost-aware leaf ordering.}',r'We keep the graph, SQ4 traversal, expansion targets, and refinement records fixed and vary only routing-tree leaf order. Layout optimization uses256 training queries; evaluation uses100 disjoint queries per dataset with three timing repetitions. BFS reduces held-out sibling hashes relative to ID and random layouts. Training-based reordering substantially reduces training proof costs but does not generalize to the held-out queries.']
    for ds in ['sift1m','deep1m']:
        d=next(x for x in reductions if x['dataset']==ds);label='SIFT' if ds=='sift1m' else 'DEEP'
        paper.append(f"On \\textsf{{{label}}}, training sibling hashes decrease by {d['train_hashes_reduction_pct']:.2f}\\%, whereas held-out sibling hashes and total VO increase by {-d['Sibling_Hashes_reduction_pct']:.2f}\\% and {-d['Total_VO_KiB_reduction_pct']:.2f}\\%, respectively; {d['test_losses']} of100 test queries incur larger routing proofs.")
    paper.append('Thus, this lightweight optimizer provides no stable online benefit over BFS in the evaluated setting. We report it as a negative ablation rather than a new contribution; these results do not rule out other objectives, training coverage, or layout algorithms.')
    (S/'analysis.tex').write_text('\n'.join(paper)+'\n')
    (S/'README.md').write_text('''# Reproduce the measured routing-layout experiment

From the inner project directory, with the existing WSL environment/data/graph caches:

```powershell
wsl -e /root/.venvs/v3db-work9/bin/python scripts/run_proof_aware_layout_20261024.py all --run-id proof-aware-layout-20261024-rerun
wsl -e /root/.venvs/v3db-work9/bin/python scripts/audit_proof_aware_layout_20261024.py --run-id proof-aware-layout-20261024-rerun
```

The same run-id resumes completed datasets and checks source/binary/graph provenance. Use a new run-id to obtain fresh timings. C++ build command: build.json. Query IDs, seed and algorithm: protocol.json, sift1m/split.json, deep1m/split.json. Source changes are isolated to src/experiments/proof_aware_layout.cpp and the two new scripts; existing benchmark/protocol sources remain untouched. Audit reuses only pure Python function definitions from scripts/audit_routing_precision_20261024.py and records its hash.

Use proof_aware_layout.tex directly in a PVLDB manuscript with `booktabs` and `graphicx` loaded. It uses a single-column table environment, two decimal places and columnwidth scaling. analysis.tex provides an honest negative-result paragraph. REPORT.md explains metrics, training/test gap, per-query regressions, construction/storage costs and limits. CSV files retain unrounded raw values; samples_all.csv includes all2400 formal authenticated trials. Per-dataset raw files, optimizer iteration history, full permutations, example VOs, roots, negative tests and traces are under results/<run-id>/. All sizes in this experiment use KiB=1024bytes, unlike older work9 tables that used decimal KB. No synthetic coefficient, prediction or workbook update is applied.
''',encoding='utf-8')
    u.write(S/'FINISHED.json',dict(passed=True,formal_authenticated_queries=2*NTEST*REPEATS*4,independent_queries_per_dataset=NTEST,layouts=4))
    for row in summary:print('RESULT',row,flush=True)
    for row in reductions:print('REDUCTIONS',row,flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('stage',choices=['build','run','report','all'],default='all',nargs='?');parser.add_argument('--run-id',default='proof-aware-layout-20261024');args=parser.parse_args();assert re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*',args.run_id)
    S=R/'experiments'/args.run_id;O=R/'results'/args.run_id;S.mkdir(parents=True,exist_ok=True);O.mkdir(parents=True,exist_ok=True);B=S/'proof_aware_layout';u.STUDY,u.RESULTS=S,O
    if args.stage in ['build','all']:build()
    if args.stage in ['run','all']:run()
    if args.stage in ['report','all']:report()
