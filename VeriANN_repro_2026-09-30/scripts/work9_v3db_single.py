"""V3DB only: one fixed query/one real proof per configuration; 95% peers frozen."""
import argparse,csv,json,os,shutil,subprocess,sys,time
from pathlib import Path
import openpyxl
from openpyxl.comments import Comment
import work9_resource_matched as util

ROOT=util.ROOT
STUDY=ROOT/'experiments/work9-v3db-single-20261020'
OUT=ROOT/'results/work9-v3db-single-20261020'
SOURCE=ROOT.parent/'work9.xlsx'
BASE=STUDY/'work9_before_single.xlsx'
CACHE=util.CACHE
DATA=util.DATA
util.STUDY=STUDY;util.RESULTS=OUT
def read(p):return json.loads(Path(p).read_text())
def write(p,x):util.write(p,x)
def env():
 e=util.environment();e['V3DB_RESULTS_DIR']=str(OUT);return e
def run(cmd,log):
 log.parent.mkdir(parents=True,exist_ok=True)
 with log.open('a') as f:r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env(),stdout=f,stderr=subprocess.STDOUT,preexec_fn=util.limit)
 if r.returncode:raise RuntimeError(log.read_text(errors='replace')[-2500:])
def recall(model,p,k):
 f=CACHE/model/'calibration.json'
 if f.exists():
  c=read(f)
  point=next((x for x in c['points'] if x['n_probe']==p and x['k']==k),None)
  if point:return dict(recall20=point['recall'],recall_source=str(f),recall_query_count=20,recall_scope='search calibration only; timing/VO use one fixed query, not 20 proofs')
 return dict(recall20=None,recall_scope='no 20-query calibration for this intermediate probe; single-query recall must not be treated as dataset recall')
def measure(ds,model,p,k):
 key=f'{model}-p{p}-k{k}'
 f=STUDY/'points'/f'{key}.json'
 if f.exists():return read(f)
 cfg=read(CACHE/model/'model.json')['config']
 job=f'{key}-q1-r1-t1';dest=OUT/job
 # Fixed query100/repeat0 chosen in advance; never pick a fastest sample.
 candidates=[OUT/job,ROOT/'results/work9-v3db-20261016'/f'{key}-q20-r3-t1',ROOT/'results/work9-v3db-reaudit-20261018'/job]
 selected=None
 for folder in candidates:
  csvfile=folder/'per_query.csv'
  if not csvfile.exists():continue
  rows=list(csv.DictReader(csvfile.open()))
  row=next((r for r in rows if int(r['query'])==100 and int(r['repeat'])==0 and r['verified']=='1'),None)
  if row and (folder/'tamper.json').exists():selected=(folder,row);break
 if selected is None:
  gate=util.static_gate(cfg,p,k)
  if gate['status']!='passed':write(f,gate);return gate
  log=STUDY/'logs'/f'{key}.log'
  try:
   run([sys.executable,ROOT/'V3DB-26VLDB/scripts/work9_measure.py','--model',model,'--probe',p,'--k',k,'--queries',1,'--repeats',1,'--attempt-large'],log)
  except RuntimeError as exc:
   if any(s in str(exc).lower() for s in ['memory allocation','memoryerror','cannot allocate','unable to allocate']):
    r=dict(status='resource_limit',label='单次出证内存受限',reason=str(exc));write(f,r);return r
   raise
  selected=(dest,next(csv.DictReader((dest/'per_query.csv').open())))
 folder,row=selected
 r=dict(status='passed',dataset=ds,model=model,config=cfg,n_probe=p,k=k,samples=1,query=100,repeat=0,
        query_ms=float(row['query_ms']),verify_ms=float(row['verify_ms']),vo_kb=float(row['vo_bytes'])/1000,
        single_query_recall=float(row['recall']),build_seconds=float(row['build_seconds']),prove_seconds=float(row['prove_seconds']),
        output=str(folder),csv_sha256=util.sha(folder/'per_query.csv'),selection='fixed query100 repeat0; not selected by runtime',
        reused_existing_sample=folder!=dest,**recall(model,p,k))
 write(f,r);return r
def save_record(key,r):write(STUDY/'records'/f'{key}.json',r);fill()
def fill():
 wb=openpyxl.load_workbook(BASE)
 records=[read(p) for p in sorted((STUDY/'records').glob('*.json'))]
 def put(ws,row,col,r,metric,percent=False):
  c=ws.cell(row,col)
  if r['status']=='passed':
   value=r.get('recall20') if metric=='recall' else r[metric]
   c.value=value*(100 if percent else 1) if value is not None else '仅有单条查询召回，未作总体估计'
  else:c.value=r.get('label','未测')
  c.comment=Comment('V3DB单次实测：固定query100，1条×1次；不是平均耗时。Recall来自另行20条搜索校准（若存在），不是20次证明。\n'+json.dumps(r,ensure_ascii=False)[:28000],'V3DB单次来源')
 for ds,cfg in DATA.items():
  ws=wb[cfg['sheet']]
  ah=next(c.row for row in ws for c in row if c.column==6 and c.row>120 and c.value=='Recall@10')
  for r in [12,26,38,50,64,75,85,97,108,118]:ws.cell(r,4,'V3DB（单次）')
  ws.cell(ah+7,4,'V3DB baseline（单次）')
  for rec in [r for r in records if r['dataset']==ds]:
   if rec['kind']=='overall':
    for col,k in [(5,'recall'),(6,'query_ms'),(7,'vo_kb'),(8,'verify_ms')]:put(ws,12,col,rec,k,col==5)
    for col,k in [(6,'recall'),(7,'query_ms'),(8,'vo_kb'),(9,'verify_ms')]:put(ws,ah+7,col,rec,k)
   elif rec['kind']=='k':
    col=6+util.KS.index(rec['k'])
    for row,k in [(97,'vo_kb'),(108,'verify_ms'),(118,'query_ms')]:put(ws,row,col,rec,k)
   elif rec['kind']=='size':
    col=6+util.SIZES.index(rec['N'])
    for row,k in [(64,'vo_kb'),(75,'verify_ms'),(85,'query_ms')]:put(ws,row,col,rec,k)
  points=[r for r in records if r['dataset']==ds and r['kind'] in ['overall','curve'] and r['status']=='passed' and r.get('recall20') is not None]
  for col in range(6,17):
   target=ws.cell(16,col).value/100
   good=[r for r in points if r['recall20']>=target-1e-12]
   if good:
    r=min(good,key=lambda r:r['n_probe'])
    for row,k in [(26,'vo_kb'),(38,'verify_ms'),(50,'query_ms')]:put(ws,row,col,r,'recall',True);put(ws,row+1,col,r,k)
   elif any(r['dataset']==ds and r['kind']=='curve_stop' for r in records):
    for row in [26,27,38,39,50,51]:ws.cell(row,col,'本机预算内未测')
 for name in ['V3DB单次说明','V3DB单次记录','V3DB低召回实测']:
  if name in wb:del wb[name]
 tab=wb.create_sheet('V3DB单次说明')
 for row in [['项目','说明'],['采样','用户指定V3DB每个配置只测1次：固定query100、repeat0。复用同配置已有该样本时不再重跑；不挑选最快值。不是平均值，无方差估计。'],['默认','仅V3DB默认低召回；VLAH/ANNProof/Ours原默认约95%的全部数据保持原样。'],['Recall','沿用同配置20条搜索校准的集合Recall@k，与1条查询的出证计时分开标注；中间probe无20条校准时不把单条召回当总体召回。'],['资源','仍为单线程，50GiB地址空间及保守证明域预算。高召回受限点不预测。'],['对照完整性','每次填表逐单元格校验非V3DB区域与本轮基准备份一致。'],['备份',str(BASE)],['状态','完成' if (STUDY/'RUN_FINISHED.json').exists() else '正在顺序实测并回填']]:tab.append(row)
 tab.column_dimensions['B'].width=140
 tab=wb.create_sheet('V3DB单次记录');tab.append(['dataset','kind','model','probe','k','status','Recall20-search','query_ms-single','verify_ms-single','VO KB-single','samples','record'])
 low=wb.create_sheet('V3DB低召回实测');low.append(['dataset','probe','20-query calibration recall','single query time ms','single verify ms','single VO KB','source'])
 for r in records:
  tab.append([r.get(k) for k in ['dataset','kind','model','n_probe','k','status','recall20','query_ms','verify_ms','vo_kb','samples']]+[json.dumps(r,ensure_ascii=False)[:32000]])
  if r['kind'] in ['overall','curve'] and r['status']=='passed':low.append([r['dataset'],r['n_probe'],r.get('recall20'),r['query_ms'],r['verify_ms'],r['vo_kb'],r['output']])
 output=STUDY/'work9_v3db_single.xlsx';wb.save(output)
 # Verify peers, including original low/high curve cells, never change.
 base=openpyxl.load_workbook(BASE);check=openpyxl.load_workbook(output)
 count=0
 for cfg in DATA.values():
  old=base[cfg['sheet']];new=check[cfg['sheet']]
  ah=next(c.row for row in old for c in row if c.column==6 and c.row>120 and c.value=='Recall@10')
  excluded={12,26,27,38,39,50,51,64,75,85,97,108,118,ah+7}
  for row in old:
   for cell in row:
    if cell.row in excluded:continue
    other=new[cell.coordinate]
    assert cell.value==other.value and cell._style==other._style,(old.title,cell.coordinate)
    assert (cell.comment.text if cell.comment else None)==(other.comment.text if other.comment else None)
    count+=1
 marker=STUDY/'last-written.txt'
 allowed={util.sha(BASE)}
 if marker.exists():allowed.add(marker.read_text().strip())
 if util.sha(SOURCE) in allowed:
  try:shutil.copy2(output,SOURCE);marker.write_text(util.sha(SOURCE))
  except PermissionError:pass
 write(STUDY/'progress.json',dict(records=len(records),non_v3db_cells_checked=count,output=str(output),delivered=util.sha(SOURCE)==util.sha(output),time=time.strftime('%Y-%m-%dT%H:%M:%S%z')))
def work():
 import fcntl
 lock=(STUDY/'runner.lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 if not BASE.exists():shutil.copy2(SOURCE,BASE)
 write(STUDY/'protocol.json',dict(proof_queries=1,repeats=1,query_id=100,threads=1,recall_queries=20,other_defaults='original ~95%; untouched',reuse='fixed query100 repeat0 only'))
 # Missing dataset first. Only one real proof; other defaults reuse the fixed
 # first already-verified sample rather than generating 60 additional proofs.
 for ds in ['glove100','deep1m','sift1m','nytimes256']:
  c=DATA[ds];r=measure(ds,util.model_name(ds),c['probe'],10)
  save_record(f'{ds}-overall',dict(r,dataset=ds,kind='overall'))
  save_record(f'{ds}-k10',dict(r,dataset=ds,kind='k',k=10))
 for ds,c in DATA.items():
  # Powers of two suffice for a small curve; no dense integer-probe sweep.
  for p in sorted({1,2,c['probe'],2*c['probe']}):
   r=measure(ds,util.model_name(ds),p,10)
   if r['status']!='passed':save_record(f'{ds}-curve-stop',dict(r,dataset=ds,kind='curve_stop'));break
   save_record(f'{ds}-curve-p{p}',dict(r,dataset=ds,kind='curve'))
 for ds,c in DATA.items():
  for k in [1,5,20,50,100]:
   r=measure(ds,util.model_name(ds),c['probe'],k);save_record(f'{ds}-k{k}',dict(r,dataset=ds,kind='k',k=k))
 for ds,c in DATA.items():
  for n in util.SIZES:
   if n>c['N'] and ds not in ['sift1m','deep1m']:continue
   source='bigann10m' if ds=='sift1m' else ('deep10m' if ds=='deep1m' and n>1000000 else ds)
   if n==c['N'] and source==ds:r=read(STUDY/'records'/f'{ds}-overall.json')
   else:
    cfg=util.config(ds,n,source);g=util.static_gate(cfg,1,10)
    if g['status']!='passed':r=g
    else:
     model=util.model_name(ds,n,source)
     if not (CACHE/model/'calibration.json').exists():run([sys.executable,ROOT/'V3DB-26VLDB/scripts/work9_calibrate.py','--dataset',ds,'--N',n,'--source',source,'--M',c['pqM'],'--K',c['K']],STUDY/'logs'/f'calibrate-{model}.log')
     points=[p for p in read(CACHE/model/'calibration.json')['points'] if p['k']==10 and p['recall']>=c['target']-1e-12]
     r=measure(ds,model,min(points,key=lambda p:p['n_probe'])['n_probe'],10) if points else dict(status='unreachable',label='未达默认低召回目标')
   save_record(f'{ds}-size{n}',dict(r,dataset=ds,kind='size',N=n))
 write(STUDY/'RUN_FINISHED.json',dict(status='single-proof V3DB plan finished; peers unchanged'));fill()
if __name__=='__main__':
 if (ROOT/'experiments/work9-v3db-lowrecall-20261020/plan.json').exists():
  raise RuntimeError('The low-recall curve writer now owns the workbook. Do not restart this older writer, which would restore the old curve columns.')
 p=argparse.ArgumentParser();p.add_argument('action',choices=['start','run','fill']);a=p.parse_args()
 STUDY.mkdir(parents=True,exist_ok=True)
 if a.action=='start':
  f=STUDY/'runner_pid.json'
  if f.exists():
   pid=read(f)['pid'];path=Path(f'/proc/{pid}/cmdline')
   if path.exists() and b'work9_v3db_single.py' in path.read_bytes():raise RuntimeError('already running')
  with (STUDY/'runner.log').open('a') as log:child=subprocess.Popen([sys.executable,__file__,'run'],env=env(),cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
  write(f,dict(pid=child.pid));print(json.dumps(dict(pid=child.pid)))
 elif a.action=='fill':fill()
 else:
  try:work()
  except BaseException as exc:
   import traceback;write(STUDY/'runner_failure.json',dict(error=str(exc),traceback=traceback.format_exc()));raise
