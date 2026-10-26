"""Measure ANNProof's 40--80% curve and write explicitly adjusted comparisons.

Raw native CSVs and historical raw-data sheets remain unchanged. Only ANNProof
comparison metrics are scaled; recall, indexes and all other methods are frozen.
"""
import argparse,csv,hashlib,json,math,os,shutil,statistics,subprocess,sys,time
from collections import defaultdict
from copy import copy
from pathlib import Path
import openpyxl
from openpyxl.comments import Comment
from openpyxl.styles import Font,PatternFill
import work9_resource_matched as util

ROOT=util.ROOT
STUDY=ROOT/'experiments/work9-annproof-low-adjusted-20261021'
OUT=ROOT/'results/work9-annproof-low-adjusted-20261021'
SOURCE=ROOT.parent/'work9.xlsx'
BACKUP=STUDY/'work9_before_annproof_adjustment.xlsx'
DATA={ds:dict(c) for ds,c in util.DATA.items()}
TARGETS=[x/100 for x in range(40,81,5)]
GRID=list(range(10,257))+list(range(264,513,8))+list(range(544,2049,32))
FACTORS={'query_ms':1.2,'vo_kb':2.0,'verify_ms':1.5}
read=util.read;write=util.write;sha=util.sha
util.STUDY=STUDY;util.RESULTS=OUT

def event(**value):
 value['time']=time.strftime('%FT%T%z');STUDY.mkdir(parents=True,exist_ok=True)
 with (STUDY/'events.jsonl').open('a') as f:f.write(json.dumps(value,ensure_ascii=False)+'\n')
 print(value.get('action'),Path(value.get('log','')).name,value.get('code',''),flush=True)
util.event=event

def cache(ds,m,efc):
 c=DATA[ds]
 return ROOT/'ANNProof-24FGCS/cache' if (m,efc)==(c['M'],c['efc']) else STUDY/'cache'

def pilot(ds,m,efc):
 c=DATA[ds];tag=f'{ds}-M{m}-efc{efc}'
 record=STUDY/'pilots'/(tag+'.json')
 if record.exists():return read(record)['points']
 out=OUT/'pilots'/tag;out.mkdir(parents=True,exist_ok=True)
 binary=ROOT/'build/multidata_pilot_ANNProof'
 cmd=[binary,'--dataset',ROOT/'dataset'/ds,'--base',c['N'],'--output',out,
      '--cache',cache(ds,m,efc),'--queries',20,'--query-offset',100,'--build-threads',1,
      '--M',m,'--ef-construction',efc,'--k',10,'--ef-search',','.join(map(str,GRID)),'--probes',1]
 util.command(cmd,STUDY/'logs'/(tag+'-pilot.log'),bounded=True,timeout=3600)
 groups=defaultdict(list)
 for r in csv.DictReader((out/'pilot.csv').open()):groups[int(r['ef_search'])].append(r)
 assert len(groups)==len(GRID)
 points=[]
 for ef,rr in groups.items():
  assert len(rr)==20 and {int(r['query']) for r in rr}==set(range(100,120))
  points.append(dict(M=m,efc=efc,ef=ef,recall=statistics.fmean(float(r['recall_at_k']) for r in rr),
                     valid=all(r['valid']=='1' for r in rr),pilot=str(out)))
 write(record,dict(dataset=ds,M=m,efc=efc,points=points,command=list(map(str,cmd)),binary_sha256=sha(binary),
                   csv_sha256=sha(out/'pilot.csv'),role='search-only calibration; timing discarded'))
 valid=[p['recall'] for p in points if p['valid']]
 print('PILOT',tag,'range',(min(valid),max(valid)) if valid else 'no complete top10; excluded',flush=True)
 return points

def choose(points,t,ds):
 good=[p for p in points if p['valid'] and p['recall']>=t-1e-12]
 if not good:return None
 close=[p for p in good if p['recall']<=t+.025+1e-12]
 default=[p for p in close if (p['M'],p['efc'])==(DATA[ds]['M'],DATA[ds]['efc'])]
 return min(default or close or good,key=lambda p:(-p['M'],p['ef'],-p['efc'],p['recall']-t))

def evaluate(ds,m,efc,efs):
 c=DATA[ds];stem=f'{ds}-M{m}-efc{efc}';out=OUT/'measurements'/stem
 meta=out/'measurement.json';prior=[]
 if meta.exists():
  prior=read(meta)['points'];missing=sorted(set(efs)-{p['ef'] for p in prior})
  if not missing:return prior
  efs=missing;out=OUT/'measurements'/(stem+'-extra-'+hashlib.sha256(json.dumps(efs).encode()).hexdigest()[:8]);meta=out/'measurement.json'
  if meta.exists():return prior+read(meta)['points']
 out.mkdir(parents=True,exist_ok=True)
 binary=ROOT/'ANNProof-24FGCS/build/annproof'
 cmd=[binary,'--dataset',ROOT/'dataset'/ds,'--base',c['N'],'--output',out,'--cache',cache(ds,m,efc),
      '--queries',20,'--query-offset',100,'--repeats',3,'--build-threads',1,'--M',m,
      '--ef-construction',efc,'--k',10,'--ef-search',','.join(map(str,efs)),'--probes',1,
      '--metric','l2','--proof','multiproof']
 write(out/'environment.json',dict(command=list(map(str,cmd)),binary_sha256=sha(binary),
  source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'ANNProof-24FGCS/annproof.cpp',ROOT/'src/paper_repro_common.hpp',ROOT/'src/verifiable_ann_common.hpp']},
  threads=1,query_offset=100,queries=20,repeats=3,M=m,ef_construction=efc,proof='unchanged multiproof'))
 util.command(cmd,STUDY/'logs'/f'{ds}-M{m}-efc{efc}-measure.log',bounded=True,timeout=3600)
 rows=list(csv.DictReader((out/'per_query.csv').open()));attacks=list(csv.DictReader((out/'tamper_tests.csv').open()))
 assert len(rows)==60*len(efs) and all(r['verified']=='1' for r in rows)
 assert len(attacks)==8 and all(r['rejected']=='1' for r in attacks)
 assert (out/'query0.vo').stat().st_size==int(rows[0]['vo_bytes'])
 (out/'PASS.txt').write_text(f'{len(rows)} verified samples; 8 tamper cases; actual wire bytes checked\n')
 pp=util.aggregate(out)
 pfile=OUT/'pilots'/f'{ds}-M{m}-efc{efc}'/'pilot.csv'
 lookup={(int(r['query']),int(r['ef_search'])):r for r in csv.DictReader(pfile.open())}
 for r in rows:
  p=lookup[int(r['query']),int(r['ef_search'])]
  for key in ['visited','expanded']:assert p[key]==r[key]
  assert abs(float(p['recall_at_k'])-float(r['recall_at_k']))<1e-10
 for p in pp:p.update(M=m,efc=efc)
 write(meta,dict(dataset=ds,N=c['N'],M=m,efc=efc,points=pp,command=list(map(str,cmd)),raw_csv_sha256=sha(out/'per_query.csv'),tamper_cases=8))
 print('MEASURED',ds,m,efc,[(p['ef'],p['recall']) for p in pp],flush=True)
 return prior+pp

def refine():
 """Test M4/efc20 and prefer richer graphs/smaller legal search budgets."""
 assert (STUDY/'RUN_FINISHED.json').exists()
 for ds,c in DATA.items():
  marker=STUDY/'refined'/(ds+'.json')
  if marker.exists():continue
  previous=read(STUDY/'records'/(ds+'.json'))
  if any(p['M']<4 for p in previous['points']):pilot(ds,4,20)
  if c['M']>8 and ds!='glove100':pilot(ds,8,80)
  points=[]
  for f in (STUDY/'pilots').glob(ds+'-*.json'):points+=read(f)['points']
  if any(not (p:=choose(points,t,ds)) or p['recall']>t+.025+1e-12 for t in TARGETS):
   points+=pilot(ds,12,120)
  selected=[dict(target=t,pilot=choose(points,t,ds)) for t in TARGETS]
  assert all(x['pilot'] and x['pilot']['recall']<=x['target']+.025+1e-12 for x in selected)
  groups=defaultdict(set)
  for x in selected:p=x['pilot'];groups[p['M'],p['efc']].add(p['ef'])
  measured={}
  for (m,efc),efs in groups.items():
   for p in evaluate(ds,m,efc,sorted(efs)):measured[m,efc,p['ef']]=p
  final=[]
  for x in selected:
   p=x['pilot'];r=measured[p['M'],p['efc'],p['ef']]
   assert abs(r['recall']-p['recall'])<1e-10
   final.append(dict(r,target=x['target'],pilot=p))
  original=STUDY/'records'/(ds+'.json');archive=STUDY/'records-before-refinement'/(ds+'.json')
  if not archive.exists():archive.parent.mkdir(exist_ok=True);shutil.copy2(original,archive)
  write(original,dict(dataset=ds,points=final));write(marker,dict(status='refined',dataset=ds))
 protocol=read(STUDY/'protocol.json');protocol['selection']='Within target..target+2.5 pp, prefer default index, otherwise largest M and smallest efSearch; ties prefer higher efConstruction. Additional M4/efc20 and M8/efc80 bridge calibration, M12/efc120 fallback; retain all candidates.'
 protocol['calibration_queries']='Same 20 IDs as timing/evaluation; no independent held-out accuracy claim.'
 write(STUDY/'protocol.json',protocol);write(STUDY/'REFINEMENT_FINISHED.json',dict(status='all four curves refined'))

def run():
 STUDY.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
 import fcntl
 lock=(STUDY/'runner.lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 if (STUDY/'failure.json').exists():(STUDY/'failure.json').rename(STUDY/f'failure-history-{time.time_ns()}.json')
 if not BACKUP.exists():shutil.copy2(SOURCE,BACKUP)
 write(STUDY/'protocol.json',dict(targets=TARGETS,queries=20,repeats=3,query_offset=100,threads=1,k=10,
  factors=FACTORS,scope='All main-table ANNProof query/VO/verify metrics adjusted once; raw evidence preserved; only 40--80% curves remeasured.',
  selection='Within target..target+2.5 pp prefer default index, otherwise highest M and smallest efSearch; report actual recall.',
  fallback_configs=[[8,80],[4,40],[4,20],[2,20],[2,10]],default_configurations=DATA,
  forbidden_shortcuts='No changed k, truncating results, dropping correct neighbors, reduced dimensions/base, skipped proof checks or timing estimates.'))
 for ds,c in DATA.items():
  file=STUDY/'records'/(ds+'.json')
  if file.exists():continue
  points=pilot(ds,c['M'],c['efc'])
  configs=([(8,80)] if c['M']>8 else [])+[(4,40),(4,20),(2,20),(2,10)]
  for m,efc in configs:
   if all((p:=choose(points,t,ds)) and p['recall']<=t+.025+1e-12 for t in TARGETS):break
   points+=pilot(ds,m,efc)
  selection=[dict(target=t,pilot=choose(points,t,ds)) for t in TARGETS]
  assert all(x['pilot'] for x in selection),(ds,'no valid configuration')
  groups=defaultdict(set)
  for x in selection:p=x['pilot'];groups[p['M'],p['efc']].add(p['ef'])
  measured={}
  for (m,efc),efs in groups.items():
   for p in evaluate(ds,m,efc,sorted(efs)):measured[m,efc,p['ef']]=p
  final=[]
  for x in selection:
   p=x['pilot'];r=measured[p['M'],p['efc'],p['ef']]
   assert abs(r['recall']-p['recall'])<1e-10
   final.append(dict(r,target=x['target'],pilot=p))
  write(file,dict(dataset=ds,points=final))
 write(STUDY/'RUN_FINISHED.json',dict(status='all four datasets measured',time=time.strftime('%FT%T%z')))

def fill():
 assert (STUDY/'RUN_FINISHED.json').exists()
 assert (STUDY/'REFINEMENT_FINISHED.json').exists()
 wb=openpyxl.load_workbook(BACKUP);changes=[];allowed=set()
 records={ds:read(STUDY/'records'/(ds+'.json')) for ds in DATA}
 def put(ws,cell,value,note,original=None):
  c=ws[cell];old=c.value if original is None else original
  existing=c.comment.text if c.comment else ''
  c.value=value;c.comment=Comment(note+'\n\n原有批注（历史来源）：\n'+existing,'ANNProof 系数调整')
  allowed.add((ws.title,c.coordinate));changes.append([ws.title,c.coordinate,old,value,note])
 for ds,cfg in DATA.items():
  ws=wb[cfg['sheet']];rec=records[ds]
  assert ws['D10'].value=='ANNProof' and ws['E8'].value=='Recall@10'
  # The Chen13 insertion shifted the old rows; require labels before mutation.
  curve=[(23,'vo_kb'),(37,'verify_ms'),(51,'query_ms')]
  for rr,key in curve:
   assert ws.cell(rr,4).value=='ANNProof' and ws.cell(rr,5).value=='Recall rate'
   header=next(r for r in range(rr-1,7,-1) if 'target recall' in str(ws.cell(r,5).value).lower())
   for p in rec['points']:
    col=next(col for col in range(6,26) if ws.cell(header,col).value==round(p['target']*100))
    note='本次真实补测：20条查询IDs100..119×3次，单线程；实际召回率不乘系数。\n'+json.dumps(p,ensure_ascii=False)
    put(ws,ws.cell(rr,col).coordinate,p['recall']*100,note)
    put(ws,ws.cell(rr+1,col).coordinate,p[key],'本单元格先替换为本次原始测量，随后按统一系数调整。\n'+json.dumps(p,ensure_ascii=False))
  metrics=[('F10','query_ms'),('G10','vo_kb'),('H10','verify_ms')]
  for rr,key in [(24,'vo_kb'),(38,'verify_ms'),(52,'query_ms'),(69,'vo_kb'),(81,'verify_ms'),(92,'query_ms'),(105,'vo_kb'),(117,'verify_ms'),(128,'query_ms')]:
   assert ws.cell(rr if rr not in [24,38,52] else rr-1,4).value=='ANNProof'
   metrics.extend((ws.cell(rr,col).coordinate,key) for col in range(6,27) if isinstance(ws.cell(rr,col).value,(int,float)))
  for addr,key in metrics:
   cell=ws[addr];raw=cell.value
   if not isinstance(raw,(int,float)):continue
   factor=FACTORS[key]
   put(ws,addr,raw*factor,f'用户指定系数调整值；不是原始实测数值。指标={key}；原始值={raw:.17g}；系数={factor}；调整值={raw*factor:.17g}。召回率不调整。原始CSV/附表保留。')
   cell.fill=PatternFill('solid',fgColor='FFF2CC');cell.font=Font(name=cell.font.name or 'Arial',size=cell.font.sz or 10,color='9C6500')
  for rr in [10,23,37,51,69,81,92,105,117,128]:put(ws,f'D{rr}','ANNProof*','* 表示性能指标为用户指定系数调整值，不是原始实测值。配置及实际召回见原始补测附表。')
  note='ANNProof* 性能为系数调整值（非原始实测）：Query×1.2、VO×2、Verify×1.5；召回率/索引不调整；原始值与配置见新增附表。'
  if ws['A3'].value is None and not isinstance(ws['A3'],openpyxl.cell.cell.MergedCell):put(ws,'A3',note,'调整口径的可见说明。')
 tab=wb.create_sheet('ANNProof调整说明')
 for row in [['项目','说明'],['性质','黄色ANNProof*指标是按用户指定系数计算的调整/情景值，不代表真实测得性能变差；不得当作原始实验测量。'],
  ['系数','Query Time ×1.2；VO Size ×2；Verify Time ×1.5。每个数值从本次备份或本次新原始测量仅乘一次。'],
  ['实测','40/45/50/55/60/65/70/75/80%目标，四数据集，k10，IDs100..119共20条×3次，单线程；实际召回率原样记录。'],
  ['旧低召回缺失原因','原图固定，ANNProof efSearch>=k=10；原最低召回SIFT67.5%、NYTimes84%、DEEP71%、GloVe36%，低目标复用了较高召回点。'],
  ['参数变化','低召回曲线可改变M、efConstruction和efSearch，并非所有点共用默认索引；逐点参数与原始测量保留。默认约95%配置和索引指标不变。'],
  ['选参规则','在目标至目标+2.5个百分点范围内优先默认图，否则优先较大M、较小efSearch；同M同ef时优先较大efConstruction。候选含M4/efConstruction20。校准和评估使用同一20条查询，不宣称独立留出评估。'],
  ['保护范围','只改主表ANNProof三类性能数值、40–80%曲线的实际召回，以及标注。其他方法、索引、已有原始记录/来源附表均保留。'],
  ['原始备份',str(BACKUP)],['原始实验日志',str(OUT)],['单元格变更','ANNProof单元格变更含前后值及原因；同一低召回格包含先替换原始测量、再乘系数两个步骤。']]:tab.append(row)
 tab.column_dimensions['A'].width=25;tab.column_dimensions['B'].width=145
 raw=wb.create_sheet('ANNProof原始补测');adj=wb.create_sheet('ANNProof调整后对比')
 columns=['Dataset','Target Recall','Actual Recall','M','efConstruction','efSearch','Queries/repeats','Query ms','VO KB','Verify ms','Nature','Source']
 raw.append(columns);adj.append(columns)
 for ds,rec in records.items():
  for p in rec['points']:
   prefix=[DATA[ds]['sheet'],p['target'],p['recall'],p['M'],p['efc'],p['ef'],'20×3 / 1 thread']
   raw.append(prefix+[p['query_ms'],p['vo_kb'],p['verify_ms'],'原始实测',p['output']])
   adj.append(prefix+[p['query_ms']*1.2,p['vo_kb']*2,p['verify_ms']*1.5,'系数调整值，非原始实测',p['output']])
 for tab in [raw,adj]:
  tab.freeze_panes='A2';tab.auto_filter.ref=tab.dimensions
  for col in ['A','G','H','I','J','K']:tab.column_dimensions[col].width=22
  tab.column_dimensions['L'].width=85
  for row in range(2,tab.max_row+1):
   for col in [2,3]:tab.cell(row,col).number_format='0.0%'
 log=wb.create_sheet('ANNProof单元格变更');log.append(['Sheet','Cell','Before','After','Reason'])
 for r in changes:log.append(r)
 # No previous auxiliary cell or other method may change.
 old=openpyxl.load_workbook(BACKUP);preserved=0
 for name in old.sheetnames:
  for row in old[name]:
   for a in row:
    if isinstance(a,openpyxl.cell.cell.MergedCell) or (name,a.coordinate) in allowed:continue
    b=wb[name][a.coordinate]
    assert a.value==b.value and a._style==b._style,(name,a.coordinate,'unrelated data/style')
    assert (a.comment.text if a.comment else None)==(b.comment.text if b.comment else None),(name,a.coordinate,'unrelated comment')
    preserved+=1
 output=STUDY/'work9_annproof_low_adjusted.xlsx';wb.save(output)
 check=openpyxl.load_workbook(output)
 final={}
 for r in changes:final[r[0],r[1]]=r[3]
 for (name,addr),expected in final.items():
  got=check[name][addr].value
  assert (math.isclose(got,expected,rel_tol=1e-13,abs_tol=1e-11) if isinstance(expected,(int,float)) else got==expected)
 marker=STUDY/'last-written.txt';valid={sha(BACKUP)}
 if marker.exists():valid.add(marker.read_text().strip())
 assert sha(SOURCE) in valid,'Workbook changed externally; review output before replacing'
 shutil.copy2(output,SOURCE);marker.write_text(sha(SOURCE))
 write(STUDY/'fill-audit.json',dict(changed_cells=len(final),change_events=len(changes),unchanged_cells_preserved=preserved,
   workbook_sha256=sha(SOURCE),backup_sha256=sha(BACKUP),factors=FACTORS,raw_curve_rows=36))
 print('WORKBOOK UPDATED',str(SOURCE),len(final),'cells; other cells preserved:',preserved)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('action',choices=['run','refine','fill']);a=p.parse_args()
 try:globals()[a.action]()
 except BaseException as exc:
  import traceback;write(STUDY/'failure.json',dict(error=str(exc),traceback=traceback.format_exc()));raise
