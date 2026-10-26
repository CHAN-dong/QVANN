"""Validate the LUT-refreshed workbook and all frozen work points."""
import argparse
import csv
import json
from pathlib import Path
import openpyxl
from work9_lut_study import ROOT,STUDY,configure


def main(completed_copy=False):
    workbook=configure()
    if completed_copy:workbook.SOURCE=workbook.OUTPUT
    # Import after configuring paths so the existing evidence validator checks
    # this new study, not the archived scalar study.
    import verify_work9_ours as validator
    validator.main()
    measurements=workbook.runs();folders={Path(r['output']) for r in measurements}
    for folder in folders:
        manifest=json.loads((folder/'manifest.json').read_text());assert manifest['method']=='VeriANN-LUT'
        equality=json.loads((folder/'scalar_equivalence.json').read_text());assert equality['status']=='PASS'
    old=openpyxl.load_workbook(workbook.TEMPLATE,data_only=True)
    new=openpyxl.load_workbook(workbook.SOURCE,data_only=True)
    audit=json.loads((STUDY/'workbook_audit.json').read_text())
    time_cells=[c for c in audit['cells'] if c['metric'] in ['query_ms','verify_ms']]
    unchanged=[c for c in audit['cells'] if c['metric'] not in ['query_ms','verify_ms']]
    assert len(time_cells)==46 and len(unchanged)==57
    for cell in unchanged:assert new['Sheet1'][cell['cell']].value==old['Sheet1'][cell['cell']].value
    for c in audit['cells']:assert 'VeriANN-LUT' in new['Sheet1'][c['cell']].comment.text
    assert len(measurements)==23 and sum(r['samples'] for r in measurements)==6900
    result=json.loads((STUDY/'final_validation.json').read_text())
    result.update(implementation='VeriANN-LUT',time_cells_remeasured=46,
                  ours_non_timing_cells_unchanged=57,all_rows_scalar_equivalent=True,
                  original_workbook_updated=not completed_copy,verified_workbook_path=str(workbook.SOURCE))
    (STUDY/'final_validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'status':'PASS','time_cells_remeasured':46,'unchanged_ours_non_timing_cells':57,
                      'baseline_cells_preserved':206,'measurements':6900}))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--completed-copy',action='store_true')
    main(parser.parse_args().completed_copy)
