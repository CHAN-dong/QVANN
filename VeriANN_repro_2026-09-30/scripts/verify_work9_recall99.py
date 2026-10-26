"""Audit workbook curve values directly against authenticated per-query measurements."""
import csv
import json
import math
import statistics
import openpyxl
from work9_recall99 import ROOT, STUDY, RESULTS, TARGETS, sha

def main():
    audit=json.loads((STUDY/'validation.json').read_text())
    book=STUDY/'work9_completed_recall99.xlsx'
    assert sha(book)==audit['output_sha256']
    wb=openpyxl.load_workbook(book)
    ws=wb['Sheet1']
    rowsets={'VLAH':[(19,20),(29,30),(39,40)],'ANNProof':[(21,22),(31,32),(41,42)],'Ours':[(23,24),(33,34),(43,44)]}
    datasets=set();counts={'formal':0,'pilot':0,'tamper_rejections':0};cells=0
    sources={}
    for method,pairs in rowsets.items():
        for phase,repeats in [('pilot',1),('evaluation',3)]:
            out=RESULTS/(method+'-'+phase)
            assert (out/'PASS.txt').exists()
            manifest=json.loads((out/'manifest.json').read_text())
            assert (manifest['base'],manifest['queries'],manifest['query_offset'],manifest['repeats'],manifest['M'],manifest['ef_construction'])==(1000000,100,100,repeats,8,100)
            datasets.add(manifest['dataset_sha256'])
            env=json.loads((out/'environment.json').read_text())
            for path,expected in env['source_sha256'].items():assert sha(ROOT/path)==expected,path
            sources[method+'-'+phase]=env['source_sha256']
            attacks=list(csv.DictReader((out/'tamper_tests.csv').open()))
            assert len(attacks)=={'ANNProof':8,'VLAH':11,'Ours':13}[method]
            assert all(a['rejected']=='1' for a in attacks)
            counts['tamper_rejections']+=len(attacks)
            rows=list(csv.DictReader((out/'per_query.csv').open()))
            assert all(r['verified']=='1' for r in rows)
            assert (out/'query0.vo').stat().st_size==int(rows[0]['vo_bytes'])
            counts['formal' if phase=='evaluation' else 'pilot']+=len(rows)
            if phase!='evaluation':continue
            records=[p for p in audit['points'] if p['method']==method]
            for col,(target,p) in enumerate(zip(TARGETS,records),6):
                group=[r for r in rows if int(r['ef_search'])==p['ef'] and int(r['probes'])==p['probes']]
                assert len(group)==300
                assert {(int(r['query']),int(r['repeat'])) for r in group}=={(q,t) for q in range(100,200) for t in range(3)}
                recall=statistics.fmean(float(r['recall_at_k']) for r in group)
                assert recall+1e-12>=target
                metrics=[statistics.fmean(int(r['vo_bytes'])/1000 for r in group),statistics.fmean(float(r['verification_ms']) for r in group),statistics.fmean(float(r['search_ms'])+float(r['vo_generation_ms']) for r in group)]
                for (rr,mr),metric in zip(pairs,metrics):
                    for row,value in [(rr,recall*100),(mr,metric)]:
                        cell=ws.cell(row,col)
                        assert math.isclose(cell.value,value,rel_tol=1e-12)
                        assert p['output'] in cell.comment.text
                        cells+=1
            assert (RESULTS/(method+'-pilot')/'trusted_roots.txt').read_bytes()==(out/'trusted_roots.txt').read_bytes()
    assert len(datasets)==1 and cells==198 and counts['formal']==9900
    assert len(wb['Recall99 measurements']._charts)==3
    assert sha(ROOT.parent/'work9_recall99_updated.xlsx')==sha(book)
    if audit['original_updated']:assert sha(ROOT.parent/'work9.xlsx')==sha(book)
    result=dict(status='PASS',cells=cells,**counts,dataset_sha256=list(datasets)[0],original_updated=audit['original_updated'],source_fingerprints=sources)
    (STUDY/'final_validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_fingerprints'}))

if __name__=='__main__':main()
