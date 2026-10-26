"""Read-only independent checks of the conditional forecast workbook."""
import json,math
from pathlib import Path
import openpyxl
from work9_resource_matched import ROOT,DATA,sha,write
S=ROOT/'experiments/work9-v3db-512GiB-20261021'
source=ROOT.parent/'work9.xlsx'
before=openpyxl.load_workbook(S/'work9_before_512GiB.xlsx')
after=openpyxl.load_workbook(source)
updates=json.loads((S/'cell-updates.json').read_text());model=json.loads((S/'model.json').read_text())
allowed={(r['sheet'],r['cell']) for r in updates}|{(c['sheet'],'A4') for c in DATA.values()}
preserved=0;estimates=0;blanks=0;recalls=0
for cfg in DATA.values():
 a=before[cfg['sheet']];b=after[cfg['sheet']]
 for row in a:
  for c in row:
   if (a.title,c.coordinate) in allowed:continue
   d=b[c.coordinate]
   assert c.value==d.value and c._style==d._style,(a.title,c.coordinate)
   assert (c.comment.text if c.comment else None)==(d.comment.text if d.comment else None)
   preserved+=1
for r in updates:
 a=before[r['sheet']][r['cell']];b=after[r['sheet']][r['cell']]
 assert not isinstance(a.value,(int,float)),('measured cell overwritten',r['sheet'],r['cell'])
 assert b.comment
 if r['value'] is None:
  assert b.value is None;blanks+=1;continue
 assert math.isclose(b.value,r['value'],rel_tol=1e-12)
 p=r['source']['choice'];c=p['config'];g=p['gates']
 assert p['guard_domain_bits']==math.ceil(math.log2(g*1.10))
 planned=1.5*model['memory_upper_coefficient_large_anchor_p95']*2**p['guard_domain_bits']+32
 assert math.isclose(planned,p['planned_machine_memory_GiB'],rel_tol=1e-12) and planned<=512
 cal=json.loads(Path(p['recall_source']).read_text())
 point=next(x for x in cal['points'] if x['n_probe']==p['n_probe'] and x['k']==10)
 assert math.isclose(point['recall'],p['recall'],rel_tol=1e-12)
 if r['source']['kind']=='curve':assert p['recall']>=r['source']['target']-1e-12
 if r['estimated']:
  assert '*' in b.number_format and b.fill.fgColor.rgb=='00FFF2CC';estimates+=1
  B=2**p['domain_bits'];T=p['n_probe']*max(c['capacity']*c['M'],c['M']*c['K'])
  q=((model['proof_seconds_per_B_logB']+model['compile_seconds_per_B_logB'])*B*p['domain_bits']+model['host_seconds_per_T_logT']*T*math.log2(max(2,T)))*1000
  assert math.isclose(q,p['query_ms'],rel_tol=1e-12)
 else:
  assert '†' in b.number_format and math.isclose(b.value,p['recall']*100,rel_tol=1e-12);recalls+=1
for ds,cfg in DATA.items():
 for row in [26,27,38,39,50,51,64,75,85]:
  for cell in after[cfg['sheet']][row]:
   if cell.column>=6:assert cell.value not in ['预算内未测','超证明域预算，未出证','超构建预算，未出证','待V3DB实测'],(ds,cell.coordinate)
result=dict(status='PASS',forecast_metric_cells=estimates,actual_search_calibration_recall_cells=recalls,unsupported_missing_cells_left_blank=blanks,all_other_main_cells_preserved=preserved,
 workload='512GiB hypothetical, one thread; forecasts not measurements',source_workbook_sha256=sha(source))
write(S/'independent-validation.json',result);print(json.dumps(result,ensure_ascii=False,indent=2))
