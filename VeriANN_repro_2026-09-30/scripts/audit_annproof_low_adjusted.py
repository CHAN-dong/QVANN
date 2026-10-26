"""Independently check raw measurements and exactly-once coefficient mapping."""
import csv,json,math,statistics
from collections import defaultdict
from pathlib import Path
import openpyxl
import work9_annproof_low_adjusted as s

def close(a,b):return isinstance(a,(int,float)) and math.isclose(a,b,rel_tol=1e-12,abs_tol=1e-10)
def run():
 assert (s.STUDY/'RUN_FINISHED.json').exists()
 old=openpyxl.load_workbook(s.BACKUP);wb=openpyxl.load_workbook(s.SOURCE)
 expected={};raw_groups={};points=[];total_samples=0;attacks=0;selected_samples=0;selected_keys=set()
 for ds,cfg in s.DATA.items():
  rec=s.read(s.STUDY/'records'/(ds+'.json'))
  assert len(rec['points'])==9 and sorted(p['target'] for p in rec['points'])==s.TARGETS
  for p in rec['points']:
   out=Path(p['output']);key=(out,p['ef'])
   assert p['recall']>=p['target']-1e-10 and p['recall']<=p['target']+.025+1e-10,(ds,p['target'],p['recall'])
   assert p['samples']==60 and p['status']=='passed'
   if out not in raw_groups:
    env=s.read(out/'environment.json');meta=s.read(out/'measurement.json')
    cmd=env['command'];assert cmd[cmd.index('--M')+1]==str(p['M']) and cmd[cmd.index('--ef-construction')+1]==str(p['efc'])
    assert cmd[cmd.index('--queries')+1]=='20' and cmd[cmd.index('--repeats')+1]=='3'
    assert cmd[cmd.index('--base')+1]==str(cfg['N']) and cmd[cmd.index('--build-threads')+1]=='1'
    assert cmd[cmd.index('--proof')+1]=='multiproof' and cmd[cmd.index('--k')+1]=='10'
    assert s.sha(Path(cmd[0]))==env['binary_sha256']
    for path,h in env['source_sha256'].items():assert s.sha(s.ROOT/path)==h
    rows=list(csv.DictReader((out/'per_query.csv').open()))
    assert s.sha(out/'per_query.csv')==meta['raw_csv_sha256']
    assert all(r['verified']=='1' for r in rows)
    negative=list(csv.DictReader((out/'tamper_tests.csv').open()));assert len(negative)==8 and all(r['rejected']=='1' for r in negative)
    assert (out/'query0.vo').stat().st_size==int(rows[0]['vo_bytes'])
    raw_groups[out]=rows;total_samples+=len(rows);attacks+=len(negative)
   rr=[r for r in raw_groups[out] if int(r['ef_search'])==p['ef']]
   assert key not in selected_keys;selected_keys.add(key);selected_samples+=len(rr)
   assert len(rr)==60 and {(int(r['query']),int(r['repeat'])) for r in rr}=={(q,t) for q in range(100,120) for t in range(3)}
   mean=lambda k:statistics.fmean(float(r[k]) for r in rr)
   for metric,value in [('recall',mean('recall_at_k')),('query_ms',mean('search_ms')+mean('vo_generation_ms')),('vo_kb',mean('vo_bytes')/1000),('verify_ms',mean('verification_ms'))]:assert close(p[metric],value)
   assert p['csv_sha256']==s.sha(out/'per_query.csv')
   points.append(dict(dataset=ds,target=p['target'],actual=p['recall'],M=p['M'],efc=p['efc'],ef=p['ef']))
  ws=wb[cfg['sheet']];base=old[cfg['sheet']]
  # Build expected values straight from the backup and raw CSV aggregation.
  for addr,factor in [('F10',1.2),('G10',2),('H10',1.5)]:expected[ws.title,addr]=base[addr].value*factor
  for row,key in [(24,'vo_kb'),(38,'verify_ms'),(52,'query_ms'),(69,'vo_kb'),(81,'verify_ms'),(92,'query_ms'),(105,'vo_kb'),(117,'verify_ms'),(128,'query_ms')]:
   for col in range(6,27):
    c=base.cell(row,col)
    if isinstance(c.value,(int,float)):expected[ws.title,c.coordinate]=c.value*s.FACTORS[key]
  for row,key in [(23,'vo_kb'),(37,'verify_ms'),(51,'query_ms')]:
   header=next(r for r in range(row-1,7,-1) if 'target recall' in str(base.cell(r,5).value).lower())
   for p in rec['points']:
    col=next(col for col in range(6,26) if base.cell(header,col).value==round(p['target']*100))
    expected[ws.title,ws.cell(row,col).coordinate]=p['recall']*100
    expected[ws.title,ws.cell(row+1,col).coordinate]=p[key]*s.FACTORS[key]
  for row in [10,23,37,51,69,81,92,105,117,128]:expected[ws.title,f'D{row}']='ANNProof*'
  if ws['A3'].value!=base['A3'].value:expected[ws.title,'A3']=ws['A3'].value
  # Accuracy/index/default configuration are unchanged, aside from the newly
  # measured curve accuracies explicitly included in expected above.
  for addr in ['E10','I10','J10']:assert ws[addr].value==base[addr].value
 preserved=0
 for name in old.sheetnames:
  for row in old[name]:
   for a in row:
    if isinstance(a,openpyxl.cell.cell.MergedCell):continue
    b=wb[name][a.coordinate]
    if (name,a.coordinate) in expected:
     value=expected[name,a.coordinate]
     assert close(b.value,value) if isinstance(value,(int,float)) else b.value==value,(name,a.coordinate)
     if isinstance(value,(int,float)) and a.row not in [23,37,51]:assert '不是原始实测' in b.comment.text
    else:
     assert a.value==b.value and a._style==b._style,(name,a.coordinate,'unchanged')
     assert (a.comment.text if a.comment else None)==(b.comment.text if b.comment else None),(name,a.coordinate,'comment')
     preserved+=1
 assert wb['ANNProof原始补测'].max_row==37 and wb['ANNProof调整后对比'].max_row==37
 assert selected_samples==2160,(selected_samples,'36 selected budgets × 60 measurements')
 report=dict(passed=True,unique_measured_points=36,selected_samples=selected_samples,raw_samples_in_selected_groups=total_samples,negative_cases=attacks,
  raw_groups=len(raw_groups),checked_modified_cells=len(expected),unchanged_cells_preserved=preserved,
  factors=s.FACTORS,points=points,backup_sha256=s.sha(s.BACKUP),workbook_sha256=s.sha(s.SOURCE))
 s.write(s.STUDY/'independent-audit.json',report)
 print(json.dumps({k:v for k,v in report.items() if k!='points'},ensure_ascii=False,indent=2))

if __name__=='__main__':run()
