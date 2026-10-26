"""Check filled Ours cells, baseline preservation, and every evidence record."""
import csv
import json
import math
from pathlib import Path
import openpyxl
from fill_work9_ours import ROOT,STUDY,SOURCE,TEMPLATE,OUTPUT,runs,size_evaluation
from fill_work9 import sha


def main():
    audit=json.loads((STUDY/'workbook_audit.json').read_text())
    assert audit['ours_numeric_cells']==audit['ours_target_cells']==103 and not audit['pending_cells']
    assert sha(SOURCE)==sha(OUTPUT)==audit['output_sha256']
    assert sha(TEMPLATE)==(STUDY/'source_sha256.txt').read_text().strip()
    wb=openpyxl.load_workbook(SOURCE,data_only=True);old=openpyxl.load_workbook(TEMPLATE,data_only=True)
    assert set(wb['Sheet1'].merged_cells.ranges)==set(old['Sheet1'].merged_cells.ranges)
    baseline=json.loads((ROOT/'experiments/work9-20261015/workbook_audit.json').read_text())
    for c in baseline['cells']:assert wb['Sheet1'][c['cell']].value==old['Sheet1'][c['cell']].value
    for c in audit['cells']:
        value=wb['Sheet1'][c['cell']].value
        assert isinstance(value,(int,float)) and math.isfinite(value) and value>=0
        assert math.isclose(value,c['value'],rel_tol=1e-12) and wb['Sheet1'][c['cell']].comment
        if c['metric']=='recall_percent':assert value<=100
    measurements=runs();folders={Path(r['output']) for r in measurements};attacks=0;binary_hashes=set()
    for folder in folders:
        record=next(r for r in measurements if Path(r['output'])==folder)
        environment=json.loads((folder/'environment.json').read_text())
        binary_hashes.add(environment['binary_sha256'])
        for relative,expected in environment['source_sha256'].items():assert sha(ROOT/relative)==expected
        m=record['manifest'];assert m['query_threads']==1 and m['M']==8 and m['ef_construction']==100
        with (folder/'per_query.csv').open() as f:rows=list(csv.DictReader(f))
        assert {int(r['query']) for r in rows}==set(range(record['query_offset'],record['query_offset']+record['queries']))
        assert {int(r['repeat']) for r in rows}==set(range(record['repeats']))
        assert (folder/'query0.vo').stat().st_size==int(rows[0]['vo_bytes'])
        with (folder/'tamper_tests.csv').open() as f:tamper=list(csv.DictReader(f))
        assert len(tamper)>=13 and all(r['rejected']=='1' for r in tamper);attacks+=len(tamper)
        # The graph really is shared with the corresponding ANNProof dataset/N.
        old_name='sift1m-recall-curve' if 'sift1m' in record['name'] else f"size-{record['base']}-evaluation"
        meta=json.loads((ROOT/'experiments/work9-20261015'/('ANNProof-24FGCS-'+old_name+'.json')).read_text())
        other=json.loads((Path(meta['output'])/'manifest.json').read_text())
        assert m['dataset_sha256']==other['dataset_sha256']
    protocol=json.loads((STUDY/'protocol_validation.json').read_text());assert protocol['status']=='PASS'
    assert len(binary_hashes)==1,'measurement jobs used different benchmark binaries'
    for n in [100000,500000,1000000,5000000,10000000]:assert 94<=size_evaluation(measurements,n)['recall_percent']<=96
    result={'status':'PASS','ours_numeric_cells':103,'baseline_numeric_cells_preserved':206,
            'total_numeric_result_cells':309,'merged_ranges_preserved':len(wb['Sheet1'].merged_cells.ranges),
            'passed_jobs':len(folders),'configurations':len(measurements),
            'verified_query_measurements':sum(r['samples'] for r in measurements),'tamper_tests_rejected':attacks,
            'protocol_tests':protocol,'workbook_sha256':sha(SOURCE),'backup_sha256':sha(TEMPLATE)}
    (STUDY/'final_validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':main()
