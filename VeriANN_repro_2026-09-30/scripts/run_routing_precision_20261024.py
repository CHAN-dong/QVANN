"""Run actual routing-precision pilots, complete authenticated trials and report."""
import argparse
import csv
import itertools
import json
import math
import re
import statistics
from pathlib import Path
import work9_resource_matched as u

R=u.ROOT;S=R/'experiments/routing-precision-20261024';O=R/'results/routing-precision-20261024'
u.STUDY,u.RESULTS=S,O;S.mkdir(parents=True,exist_ok=True)
REFERENCE=u.read(R/'experiments/work9-quantization-20261023/protocol.json')
DATA={ds:REFERENCE['datasets'][ds] for ds in ['sift1m','deep1m']}
SOURCE=R/'src/experiments/routing_precision.cpp';BINARY=S/'routing_precision'

def build():
    original=R/'build/veriann_lut_benchmark.cpp'
    prefix=original.read_text().split('int main(int argc,char**argv)try{')[0]
    u.write(S/'protocol.json',dict(date='2026-10-24',datasets=DATA,k=10,queries=20,query_offset=100,repeats=3,threads=1,
        fixed_E={ds:c['E'] for ds,c in DATA.items()},matched_recall_target=.95,matched_gap_preference=.005,calibration_E=list(range(1,257)),
        precision='Exact uses unquantized original float32 vectors and the same scalar float32 squared-L2 accumulation as original QVANN/ground truth; SQ4 uses original quantized scores. All upper-level and frontier decisions use the requested precision.',
        authentication='Both modes use the identical original QVANN route-SQ4/delta-SQ6/raw BFS Merkle roots and canonical multiproofs. Exact additionally supplies/opens the raw tree for all scored candidates; raw routing and final-result records are deduplicated. Cached exact routing distances are reused in final ranking.',
        result_certificate='Same exact top-k ranking and canonical SQ4->SQ6->raw final refinement checks. Additional Exact raw records are justified only by replay coverage; SQ4 forbids extras.',
        framing='A4-byte routing mode tag is added to both modes. Removing it from SQ4 gives byte-identical original QVANN VO accepted by the original verifier.',
        tests='All formal queries compare server/client ordered expansion fingerprints; authenticated SQ4 transcripts with full raw disclosure must fail Exact replay after all Merkle checks pass; ordinary payload/proof/query/budget tampering also rejected.',
        limitations='20 independent query IDs repeated3 times. Calibration and timing use the same query IDs, not a held-out recall claim. Defaults may exceed95% on this20-query subset. No coefficient scaling, prediction, smoothing, graph rebuilding or workbook modification.'))
    if BINARY.exists():
        old=u.read(S/'build.json');assert old['source_sha256']==u.sha(SOURCE) and old['original_sha256']==u.sha(original),'Archive prior runs before changing source'
        return
    (S/'routing_precision_original_include.cpp').write_text(prefix)
    cmd=u.read(R/'experiments/work9-quantization-20261023/build.json')['command']
    cmd=[str(SOURCE) if str(x).endswith('/scripts/quantization_refinement_probe.cpp') else '-I'+str(S) if str(x).startswith('-I') and str(x).endswith('/experiments/work9-quantization-20261023') else x for x in cmd]
    cmd[cmd.index('-o')+1]=str(BINARY)
    u.command(cmd,S/'build.log',timeout=300)
    u.write(S/'build.json',dict(command=cmd,source_sha256=u.sha(SOURCE),original_sha256=u.sha(original),include_sha256=u.sha(S/'routing_precision_original_include.cpp'),binary_sha256=u.sha(BINARY)))

def command(ds,phase,extra):
    cfg=DATA[ds];m=cfg['manifest'];out=O/ds/phase
    assert u.sha(cfg['graph'])==cfg['graph_sha256'],'Historical graph changed'
    if (out/'PASS.txt').exists():
        assert u.read(out/'environment.json')['binary_sha256']==u.sha(BINARY)
        return
    cmd=[BINARY,'--dataset',R/'dataset'/ds,'--base',m['base'],'--M',m['M'],'--ef-construction',m['ef_construction'],'--cache',Path(cfg['graph']).parent,'--output',out,'--queries',20,'--query-offset',100,'--repeats',3,'--build-threads',1,'--k',10,'--ef-search',cfg['E'],'--phase',phase,*extra]
    u.command(cmd,S/(ds+'-'+phase+'.log'),bounded=True,timeout=7200)
    assert u.sha(cfg['graph'])==cfg['graph_sha256']
    u.write(out/'environment.json',dict(command=list(map(str,cmd)),binary_sha256=u.sha(BINARY),graph_sha256=cfg['graph_sha256']))

def pilot():
    calibration={}
    for ds in DATA:
        command(ds,'pilot',['--max-E',256])
        rows=list(csv.DictReader((O/ds/'pilot/calibration.csv').open()));assert len(rows)==2*256*20
        points={}
        for mode in ['Exact','SQ4']:
            for E in range(1,257):
                rr=[r for r in rows if r['routing']==mode and int(r['E'])==E];assert len(rr)==20
                assert {int(r['query']) for r in rr}==set(range(100,120))
                points[mode,E]=dict(E=E,recall=sum(int(r['hits']) for r in rr)/200,candidates=statistics.fmean(int(r['candidates']) for r in rr))
        pairs=[(points['Exact',a],points['SQ4',b]) for a,b in itertools.product(range(1,257),repeat=2)]
        close=[(a,b) for a,b in pairs if abs(a['recall']-b['recall'])<=.005+1e-12]
        candidates=close or pairs
        def key(pair):
            a,b=pair;da,db=abs(a['recall']-.95),abs(b['recall']-.95)
            return (round(max(da,db),12),round(da+db,12),round(abs(a['recall']-b['recall']),12),int(a['recall']<.95-1e-12)+int(b['recall']<.95-1e-12),a['E']+b['E'],a['E'],b['E'])
        a,b=min(candidates,key=key)
        calibration[ds]=dict(Exact=a,SQ4=b,gap=abs(a['recall']-b['recall']),gap_within_half_pp=bool(close),target=.95,
            selection='Prefer gap<=0.5 percentage points; minimize maximum deviation from95%, then total deviation, gap, below-target count, total E. Enumerate every integer E1..256; no monotonicity assumption.',source=str(O/ds/'pilot/calibration.csv'),source_sha256=u.sha(O/ds/'pilot/calibration.csv'))
        print('MATCHED_RECALL_SELECTED',ds,calibration[ds],flush=True)
    u.write(S/'calibration.json',calibration)

def formal():
    selected=u.read(S/'calibration.json')
    for ds in DATA:command(ds,'formal',['--matched-exact',selected[ds]['Exact']['E'],'--matched-sq4',selected[ds]['SQ4']['E']])

def report():
    chosen=u.read(S/'calibration.json');records=[];all_rows=[]
    for ds,cfg in DATA.items():
        out=O/ds/'formal';identity=u.read(out/'identity.json')
        assert identity['dataset_sha256']==cfg['manifest']['dataset_sha256']
        assert identity['legacy_checks']==120 and identity['semantic_witnesses']==2 and identity['samples']==240
        rows=list(csv.DictReader((out/'samples.csv').open()));assert len(rows)==240
        pilot_rows={(r['routing'],int(r['E']),int(r['query'])):r for r in csv.DictReader((O/ds/'pilot/calibration.csv').open())}
        truth={int(r['query']):set(map(int,r['answer'].split())) for r in csv.DictReader((out/'ground_truth.csv').open())}
        for row in rows:
            q=int(row['query']);a=list(map(int,row['answer'].split()));ref=pilot_rows[row['routing'],int(row['E']),q]
            assert row['valid']=='1' and row['trace_sha256']==row['client_trace_sha256']==ref['trace_sha256'] and row['answer']==ref['answer']
            assert len(a)==len(set(a))==10 and len(set(a)&truth[q])==int(row['hits'])
            assert math.isclose(int(row['hits'])/10,float(row['recall']),abs_tol=1e-12)
            assert int(row['raw_records'])==int(row['final_raw_records'])+int(row['routing_only_raw_records'])
            if row['routing']=='Exact':assert int(row['raw_records'])==int(row['candidates'])
            else:assert int(row['routing_only_raw_records'])==0
            all_rows.append(dict(dataset=ds,N=cfg['manifest']['base'],M=cfg['manifest']['M'],ef_construction=cfg['manifest']['ef_construction'],threads=1,**row))
        for setting in ['Fixed E','Matched R']:
            for mode in ['Exact','SQ4']:
                rr=[r for r in rows if r['setting']==setting and r['routing']==mode];assert len(rr)==60
                assert {(int(r['query']),int(r['repeat'])) for r in rr}=={(q,t) for q in range(100,120) for t in range(3)}
                E=cfg['E'] if setting=='Fixed E' else chosen[ds][mode]['E'];assert all(int(r['E'])==E for r in rr)
                rec=dict(dataset=ds,Dataset='SIFT' if ds=='sift1m' else 'DEEP',Setting=setting,Routing=mode,E=E,N=cfg['manifest']['base'],D=identity['D'],M=cfg['manifest']['M'],ef_construction=cfg['manifest']['ef_construction'],samples=60,independent_queries=20,threads=1,source=str(out/'samples.csv'),source_sha256=u.sha(out/'samples.csv'))
                for metric in ['recall','candidates','vo_bytes','verify_ms','auth_ms','replay_ms','result_ms','query_ms','raw_records','final_raw_records','routing_only_raw_records','proof_bytes']:
                    rec[metric]=statistics.fmean(float(r[metric]) for r in rr)
                    if metric.endswith('_ms'):rec[metric+'_stdev']=statistics.stdev(float(r[metric]) for r in rr)
                rec['R_pct']=rec['recall']*100;rec['VO_KB']=rec['vo_bytes']/1000;rec['V_ms']=rec['verify_ms'];rec['C']=rec['candidates'];records.append(rec)
        tests=list(csv.DictReader((out/'tests.csv').open()));assert all(t['rejected']=='1' for t in tests)
        semantic=[t for t in tests if t['test']=='authenticated_SQ4_trace_is_not_Exact'];assert len(semantic)==2 and all(t['auth_passed']=='1' for t in semantic)
    assert len(records)==8 and len(all_rows)==480
    u.write(S/'summary.json',records)
    for filename,rr in [('samples_all.csv',all_rows),('summary.csv',records)]:
        with (S/filename).open('w',newline='') as f:
            writer=csv.DictWriter(f,fieldnames=list(rr[0]));writer.writeheader();writer.writerows(rr)
    lines=[r'\begin{table}[t]',r'\centering',r'\caption{Impact of routing precision ($k=10$). $R$: Recall@10; $|C|$: discovered candidates. VO is in KB and $V$ in ms. Best and second-best costs within each dataset/setting are bold and underlined, respectively.}',r'\label{tab:routing_precision}',r'\vspace{-0.15cm}',r'\footnotesize',r'\setlength{\tabcolsep}{2pt}',r'\renewcommand{\arraystretch}{1.1}',r'\resizebox{\columnwidth}{!}{%',r'\begin{tabular}{@{}lllrrrrr@{}}',r'\toprule',r'Dataset & Setting & Routing & $E$ & $R$ (\%) & $|C|$ & VO (KB) & $V$ (ms) \\',r'\midrule']
    for index,rec in enumerate(records):
        pair=[r for r in records if r['dataset']==rec['dataset'] and r['Setting']==rec['Setting']]
        def cost(metric):
            value=f'{rec[metric]:.2f}';ordered=sorted(set(f'{r[metric]:.2f}' for r in pair),key=float)
            return r'\textbf{'+value+'}' if value==ordered[0] else r'\underline{'+value+'}'
        if index==4:lines.append(r'\midrule')
        elif index in [2,6]:lines.append(r'\addlinespace[2pt]')
        lines.append(' & '.join([rec['Dataset'] if index in [0,4] else '',rec['Setting'] if index%2==0 else '',rec['Routing'],str(rec['E']),f"{rec['R_pct']:.2f}",f"{rec['C']:.2f}",cost('VO_KB'),cost('V_ms')])+r' \\')
    lines += [r'\bottomrule',r'\end{tabular}%',r'}',r'\end{table}','']
    (S/'routing_precision.tex').write_text('\n'.join(lines))
    paragraphs=['# Impact of Routing Precision — measured results (2026-10-24)','',
        'All rows use the original fixed graph per dataset, query IDs100..119, k10,20 queries×3 repetitions and one thread. E is a unique-expanded-node budget, not Faiss efSearch. KB=1000bytes. Calibration enumerates every integer E1..256 on the same20 queries, so matched recall is not a held-out accuracy claim.','',
        'Both modes share precisely the same trusted route/delta/raw Merkle roots and final progressive result rules. Exact authenticates all raw vectors used by precise upper-layer/frontier decisions, transmits each once, and reuses computed exact distances for final ranking. SQ4 is byte-identical to original QVANN after removing a common4-byte mode tag. There is no server graph/database at the client.','']
    for ds in DATA:
        for setting in ['Fixed E','Matched R']:
            a=next(r for r in records if r['dataset']==ds and r['Setting']==setting and r['Routing']=='Exact');b=next(r for r in records if r['dataset']==ds and r['Setting']==setting and r['Routing']=='SQ4')
            paragraphs.append(f"{a['Dataset']} / {setting}: Exact E={a['E']}, R={a['R_pct']:.2f}%, C={a['C']:.2f}, VO={a['VO_KB']:.2f}KB, V={a['V_ms']:.2f}ms; SQ4 E={b['E']}, R={b['R_pct']:.2f}%, C={b['C']:.2f}, VO={b['VO_KB']:.2f}KB, V={b['V_ms']:.2f}ms. SQ4 changes recall by {b['R_pct']-a['R_pct']:+.2f}pp and candidates by {(b['C']/a['C']-1)*100:+.2f}%; VO reduction={(1-b['VO_KB']/a['VO_KB'])*100:.2f}%, V reduction={(1-b['V_ms']/a['V_ms'])*100:.2f}%.")
            paragraphs.append('')
    paragraphs += ['Routing scores can change the expansion trajectory and candidate count at fixed E; they do not imply equal recall. Matching measured recall permits different E. Communication differences include all required routing disclosure and raw-tree authentication; they are not payload-only estimates. Verification time includes decoding, all Merkle checks, actual requested-precision replay and final ranking/certificate checks. Values above are preserved whether or not a hypothesis is supported.','',
        'Fixed E deliberately retains SIFT128/DEEP64 from current defaults; the20-query subset can have higher recall than the historical SIFT100-query mean. Each recall step is0.5 percentage points across20 independent top10 queries. Repeated queries do not increase the independent accuracy sample size.','',
        'Reproduce: `wsl -e /root/.venvs/v3db-work9/bin/python scripts/run_routing_precision_20261024.py all`, then `wsl -e /root/.venvs/v3db-work9/bin/python scripts/audit_routing_precision_20261024.py`.','',
        'New code: `src/experiments/routing_precision.cpp`, `scripts/run_routing_precision_20261024.py`, and `scripts/audit_routing_precision_20261024.py`. The original QVANN source, datasets, graph caches, workbook and previous experiments are not modified. Detailed protocol, compilation/source/binary hashes, chosen E, raw query records, exact expansion scores, complete sample VOs, semantic rejection witnesses and analyses are stored with this study.']
    (S/'REPORT.md').write_text('\n'.join(paragraphs)+'\n')
    u.write(S/'FINISHED.json',dict(passed=True,points=8,formal_authenticated_queries=480,legacy_equivalence_checks=240,semantic_auth_passing_replay_rejections=4))
    for rec in records:print('RESULT',{k:rec[k] for k in ['Dataset','Setting','Routing','E','R_pct','C','VO_KB','V_ms']},flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('stage',choices=['build','pilot','formal','report','all'],default='all',nargs='?');parser.add_argument('--run-id',default='routing-precision-20261024',help='new name creates an isolated fresh run; existing name resumes validated checkpoints');args=parser.parse_args()
    assert re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*',args.run_id),'Invalid run-id'
    S=R/'experiments'/args.run_id;O=R/'results'/args.run_id;S.mkdir(parents=True,exist_ok=True);u.STUDY,u.RESULTS=S,O;BINARY=S/'routing_precision'
    if args.stage in ['build','all']:build()
    if args.stage in ['pilot','all']:pilot()
    if args.stage in ['formal','all']:formal()
    if args.stage in ['report','all']:report()
