"""Expose existing low recall points in main curves and add four light points/dataset."""
import argparse,csv,json,os,shutil,subprocess,sys,time
from pathlib import Path
from copy import copy
import openpyxl
from openpyxl.comments import Comment
from openpyxl.chart import ScatterChart,Reference,Series
import work9_v3db_single as single

ROOT=single.ROOT
OLD=single.STUDY
STUDY=ROOT/'experiments/work9-v3db-lowrecall-20261020'
OUT=ROOT/'results/work9-v3db-lowrecall-20261020'
SOURCE=single.SOURCE
BASE=STUDY/'work9_before_lowrecall.xlsx'
DATA=single.DATA
LOW=[.15,.20,.25,.30,.35,.40,.45,.50,.55]
HIGH=[.60,.65,.70,.75,.80,.85,.90,.95,.97,.98,.99]
TARGETS=LOW+HIGH
JOBS={
 'sift1m':[('sift1m-1000000',1),('sift1m-1000000',4),('sift1m-1000000',8),('sift1m-1000000-M32',1)],
 'deep1m':[('deep1m-1000000',1),('deep1m-1000000',2),('deep1m-1000000',4),('deep1m-1000000-M24',1)],
 'nytimes256':[('nytimes256-289761',1),('nytimes256-289761',2),('nytimes256-289761',4),('nytimes256-289761-M64',1)],
 'glove100':[('glove100-1183514',1),('glove100-1183514',2),('glove100-1183514',4),('glove100-1183514-M25',1)],
}
single.STUDY=STUDY;single.OUT=OUT;single.util.STUDY=STUDY;single.util.RESULTS=OUT
read=single.read;write=single.write

def points():
 found={}
 for folder in [OLD/'records',STUDY/'records']:
  for f in folder.glob('*.json'):
   r=read(f)
   if r.get('status')=='passed' and r.get('kind') in ['curve','overall'] and r.get('recall20') is not None:
    assert r['samples']==1
    found[r['dataset'],r['model'],r['n_probe'],r['k']]=r
 return sorted(found.values(),key=lambda r:(r['dataset'],r['recall20'],r['query_ms']))
def baseline_low(ds,method,target):
 f=ROOT/'experiments/work9-resource-matched-20261020/records'/f'{ds}-{method}-curve.json'
 if not f.exists():return None
 r=read(f);good=[p for p in r['points'] if p.get('status')=='passed' and p['recall']>=target-1e-12]
 return min(good,key=lambda p:(p['recall'],p['query_ms'])) if good else None

def fill():
 wb=openpyxl.load_workbook(BASE);records=points();audit=[]
 for ds,cfg in DATA.items():
  ws=wb[cfg['sheet']]
  before=openpyxl.load_workbook(BASE)[cfg['sheet']]
  # Move only curve cells; every original >=60% point keeps its value/comment.
  for header in [16,28,40]:
   for col,t in enumerate(TARGETS,6):
    ws.cell(header,col)._style=copy(before.cell(header,6)._style);ws.cell(header,col,t*100)
  for start,metric in [(20,'vo_kb'),(32,'verify_ms'),(44,'query_ms')]:
   for i,method in enumerate(['VLAH','ANNProof','Ours']):
    for offset in [0,1]:
     row=start+2*i+offset
     for j,t in enumerate(TARGETS):
      cell=ws.cell(row,6+j)
      if t in HIGH:
       prior=before.cell(row,6+HIGH.index(t));cell.value=prior.value;cell._style=copy(prior._style);cell.comment=copy(prior.comment)
      else:
       r=baseline_low(ds,method,t);cell._style=copy(before.cell(row,6)._style)
       cell.value=(r['recall']*100 if offset==0 else r[metric]) if r else '待低召回曲线实测'
       cell.comment=Comment('仅扩展曲线，不改变该方法约95%的默认配置。\n'+json.dumps(r,ensure_ascii=False),'低召回曲线来源')
   candidates=[r for r in records if r['dataset']==ds]
   for j,t in enumerate(TARGETS):
    good=[r for r in candidates if r['recall20']>=t-1e-12]
    # Across PQ configurations, retain the cheapest measured single proof that
    # meets the target, never interpolate or relabel actual recall as target.
    r=min(good,key=lambda r:(r['query_ms'],r['recall20'])) if good else None
    for row,value in [(start+6,r['recall20']*100 if r else '预算内未测'),(start+7,r[metric] if r else '预算内未测')]:
     cell=ws.cell(row,6+j);cell.value=value;cell._style=copy(before.cell(row,6)._style)
     cell.comment=Comment('固定query100单次真实出证；Recall为同配置20条搜索校准。可调整PQ M/K与probe；不代表主表默认索引配置。\n'+json.dumps(r,ensure_ascii=False),'V3DB低召回实测')
     audit.append([ws.title,cell.coordinate,value,json.dumps(r,ensure_ascii=False)])
  ws['E16']='Target Recall (%) — expanded low-recall curve'
 # Audit defaults, index/size/k/ablation data verbatim against this snapshot.
 checkbase=openpyxl.load_workbook(BASE);checks=0
 for cfg in DATA.values():
  old=checkbase[cfg['sheet']];new=wb[cfg['sheet']]
  curve_rows={16,28,40}|set(range(20,28))|set(range(32,40))|set(range(44,52))
  for row in old:
   for a in row:
    if a.row in curve_rows:continue
    b=new[a.coordinate]
    assert a.value==b.value and a._style==b._style,(old.title,a.coordinate)
    assert (a.comment.text if a.comment else None)==(b.comment.text if b.comment else None)
    checks+=1
 for name in ['低召回曲线说明','V3DB低召回新增记录','低召回曲线单元格来源']:
  if name in wb:del wb[name]
 tab=wb.create_sheet('低召回曲线说明')
 for row in [['项目','说明'],['范围','只扩展Recall@10 vs VO/Verify/Query三组曲线。四个数据集全部方法的默认、索引、规模、K、消融数据均与本轮前文件逐项一致。'],['新增目标','15/20/25/30/35/40/45/50/55%，保留原60–99%列及原方法实测。参数离散，填实际召回，不强改成目标值。'],['V3DB','补测四数据集各4个更轻PQ/低probe配置，每配置固定query100出证1次；已有相同固定样本直接复用，不重复出证。'],['Recall与时间','Recall为20条查询搜索校准；时间/VO为1条查询×1次，不是均值。PQ配置可变；每个目标选满足目标的已测最小查询时间配置，此选择来自单次样本，存在噪声，不声称统计最优。'],['对照方案','仅使用已经验证通过的完整20×3低召回曲线记录；没有记录就标待测。不改变默认约95%配置。'],['位置','低召回点现已进入四个主表三组曲线；新增附表给出全部实测点和参数。'],['校验',f'非曲线区域{checks}个单元格与备份一致。'],['备份',str(BASE)],['状态','新增计划完成' if (STUDY/'RUN_FINISHED.json').exists() else '正在逐个配置出证并回填']]:tab.append(row)
 tab.column_dimensions['B'].width=140
 tab=wb.create_sheet('V3DB低召回新增记录')
 tab.append(['dataset','M','K','probe','Recall20-search','query_ms-single','verify_ms-single','VO KB-single','samples','model','source'])
 for r in records:tab.append([r['dataset'],r['config']['M'],r['config']['K'],r['n_probe'],r['recall20'],r['query_ms'],r['verify_ms'],r['vo_kb'],1,r['model'],r['output']])
 for i,(label,col) in enumerate([('Query ms',6),('Verify ms',7),('VO KB',8)]):
  chart=ScatterChart();chart.title='V3DB low recall: '+label;chart.x_axis.title='Actual Recall@10';chart.x_axis.numFmt='0%';chart.y_axis.title=label;chart.width=23;chart.height=12
  for ds in DATA:
   ix=[j+2 for j,r in enumerate(records) if r['dataset']==ds]
   if ix:chart.series.append(Series(Reference(tab,min_col=col,min_row=min(ix),max_row=max(ix)),Reference(tab,min_col=5,min_row=min(ix),max_row=max(ix)),title=DATA[ds]['sheet']))
  tab.add_chart(chart,f'M{1+i*24}')
 tab=wb.create_sheet('低召回曲线单元格来源');tab.append(['sheet','cell','value','source'])
 for row in audit:tab.append(row)
 output=STUDY/'work9_lowrecall.xlsx';wb.save(output)
 marker=STUDY/'last-written.txt';allowed={single.util.sha(BASE)}
 if marker.exists():allowed.add(marker.read_text().strip())
 if single.util.sha(SOURCE) in allowed:
  try:shutil.copy2(output,SOURCE);marker.write_text(single.util.sha(SOURCE))
  except PermissionError:pass
 write(STUDY/'progress.json',dict(points=len(records),non_curve_cells_checked=checks,delivered=single.util.sha(SOURCE)==single.util.sha(output),time=time.strftime('%Y-%m-%dT%H:%M:%S%z')))

def work():
 import fcntl
 lock=(STUDY/'runner.lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 if not BASE.exists():shutil.copy2(SOURCE,BASE)
 write(STUDY/'plan.json',dict(jobs=JOBS,timing_queries=1,repeats=1,recall_queries=20,default_cells='frozen'))
 fill()
 for ds,jobs in JOBS.items():
  for model,p in jobs:
   r=single.measure(ds,model,p,10)
   write(STUDY/'records'/f'{model}-p{p}.json',dict(r,dataset=ds,kind='curve'));fill()
 write(STUDY/'RUN_FINISHED.json',dict(status='all additional low recall jobs executed'));fill()

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('action',choices=['start','run','fill']);a=p.parse_args();STUDY.mkdir(parents=True,exist_ok=True)
 if a.action=='start':
  f=STUDY/'runner_pid.json'
  if f.exists():
   pid=read(f)['pid'];path=Path(f'/proc/{pid}/cmdline')
   if path.exists() and b'work9_v3db_lowrecall.py' in path.read_bytes():raise RuntimeError('already running')
  with (STUDY/'runner.log').open('a') as log:child=subprocess.Popen([sys.executable,__file__,'run'],cwd=ROOT,env=single.env(),stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
  write(f,dict(pid=child.pid));print(json.dumps(dict(pid=child.pid)))
 elif a.action=='fill':fill()
 else:
  try:work()
  except BaseException as exc:
   import traceback;write(STUDY/'runner_failure.json',dict(error=str(exc),traceback=traceback.format_exc()));raise
