"""Explicit scenario estimates, kept distinct from actual experimental results.

First remove dominated ANNProof operating points using complete measured tuples.
Then fit remaining comparison curves under the USER-REQUESTED nondecreasing
trend assumption. This assumption is not an algorithmic monotonicity theorem.
No estimate is presented as a new measurement; historical data stays intact.
"""
import argparse,hashlib,json,math,shutil
from collections import defaultdict
from pathlib import Path
import openpyxl
from openpyxl.comments import Comment
from openpyxl.styles import PatternFill,Font,Alignment
R=Path(__file__).resolve().parents[1];SOURCE=R.parent/'work9.xlsx'
S=R/'experiments/work9-trend-revision-20261021';S.mkdir(parents=True,exist_ok=True)
BACKUP=S/'work9_before_trend_revision.xlsx'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
args=argparse.ArgumentParser();args.add_argument('--apply',action='store_true');args=args.parse_args()
assert not (S/'APPLIED.json').exists(),'Already applied. Do not double-adjust.'
if not BACKUP.exists():shutil.copy2(SOURCE,BACKUP)
assert sha(SOURCE)==sha(BACKUP),'Workbook changed since backup.'
w=openpyxl.load_workbook(BACKUP);original=openpyxl.load_workbook(BACKUP)
names=['SIFT1M','NYTimes-256','GloVe-100','DEEP1M'];events=[];predicted=set();selected=set();label_cells=set()
def setvalue(s,r,c,v,kind,why,source=None):
 cell=s.cell(r,c);old=cell.value
 if isinstance(old,(int,float)) and math.isclose(old,v,rel_tol=1e-12,abs_tol=1e-12):return
 previous=cell.comment.text if cell.comment else ''
 events.append(dict(sheet=s.title,cell=cell.coordinate,before=old,after=v,kind=kind,reason=why,source=source))
 cell.value=v
 cell.comment=Comment(kind+'：'+why+'\n本次原值='+str(old)+'；修订值='+str(v)+'\n来源/拟合输入：'+json.dumps(source,ensure_ascii=False)+'\n\n修订前批注（历史，不代表修订后来源）：\n'+previous,'Codex')
 if kind=='预测修订':
  predicted.add((s.title,cell.coordinate));cell.fill=PatternFill('solid',fgColor='FCE4D6');cell.font=Font(color='C00000');cell.number_format='0.000" †预测"'
 else:
  selected.add((s.title,cell.coordinate));cell.fill=PatternFill('solid',fgColor='E2F0D9')
# Complete-tuple replacement: do not mix three metrics from different runs.
for sn in names:
 s=w[sn];points=[]
 for c in range(6,26):
  recall=s.cell(23,c).value;cost=[s.cell(r,c).value for r in [24,38,52]]
  if isinstance(recall,(int,float)) and all(isinstance(x,(int,float)) for x in cost):points.append(dict(col=c,recall=recall,cost=cost))
 for p in points:
  candidates=[q for q in points if q['recall']>=p['recall']-1e-10 and all(a<=b*(1+1e-12) for a,b in zip(q['cost'],p['cost'])) and any(a<b*(1-1e-12) for a,b in zip(q['cost'],p['cost']))]
  if not candidates:continue
  q=min(candidates,key=lambda q:(q['cost'][2],q['cost'][0],q['cost'][1]))
  for r in [23,24,37,38,51,52]:
   src=original[sn].cell(r,q['col']);why=f'原配置被已有更高/相同召回配置同时在VO、查询、验证三指标支配，整组改选原表{src.column_letter}列。实际召回改为{q["recall"]}%，并非新测量原来的低召回率；ANNProof既有系数已包含，未重复乘。'
   setvalue(s,r,p['col'],src.value,'已有测量配置重选',why,dict(cell=src.coordinate,comment=src.comment.text if src.comment else ''))
def pava(values):
 """Equal-weight least-squares fit in log space (relative-error scale)."""
 blocks=[]
 for i,y in enumerate(values):
  blocks.append([i,i,math.log(y),1])
  while len(blocks)>1 and blocks[-2][2]/blocks[-2][3]>blocks[-1][2]/blocks[-1][3]:
   b=blocks.pop();a=blocks.pop();blocks.append([a[0],b[1],a[2]+b[2],a[3]+b[3]])
 out=[None]*len(values)
 for lo,hi,total,n in blocks:
  for i in range(lo,hi+1):out[i]=math.exp(total/n)
 return out
curves=[]
for rr,metric in [(21,'VO KB'),(35,'verify ms'),(49,'query ms')]:
 for i,method in enumerate(['VLAH','ANNProof','Ours','V3DB']):curves.append(dict(panel='Recall@10',row=rr+2*i+1,label=rr+2*i,method=method,metric=metric,cols=list(range(6,26)),xrow=rr+2*i))
for panel,starts,cols,header in [('Dataset size',[(68,'VO KB'),(80,'verify ms'),(91,'query ms')],list(range(6,11)),[67,79,90]),('K',[(104,'VO KB'),(116,'verify ms'),(127,'query ms')],list(range(6,12)),[103,115,126])]:
 for (start,metric),hr in zip(starts,header):
  for i,method in enumerate(['VLAH','ANNProof','Ours','V3DB','Chen13-MR']):curves.append(dict(panel=panel,row=start+i,label=start+i,method=method,metric=metric,cols=cols,xrow=hr))
fits=[]
for sn in names:
 s=w[sn]
 for curve in curves:
  r=curve['row'];groups=defaultdict(list)
  for c in curve['cols']:
   v=s.cell(r,c).value;x=s.cell(curve['xrow'],c).value
   if not isinstance(v,(int,float)):continue
   assert math.isfinite(v) and v>0,(sn,r,c,v)
   if curve['panel']!='Recall@10':x=c
   if not isinstance(x,(int,float)):continue
   groups[x].append((c,v))
  xs=sorted(groups)
  if len(xs)<2:continue
  # Duplicate recall points count once, not once per repeated target column.
  ys=[math.exp(sum(math.log(v) for c,v in groups[x])/len(groups[x])) for x in xs]
  if all(b>=a*(1-1e-12) for a,b in zip(ys,ys[1:])) and all(max(v for c,v in g)<=min(v for c,v in g)*(1+1e-12) for g in groups.values()):continue
  fitted=pava(ys)
  inputs=[dict(x=x,cells=[s.cell(r,c).coordinate for c,v in groups[x]],values=[v for c,v in groups[x]]) for x in xs]
  changed=False
  for x,estimate,y in zip(xs,fitted,ys):
   # Round modified estimates to three significant digits, never raw values.
   differs=any(not math.isclose(v,estimate,rel_tol=1e-10,abs_tol=1e-12) for c,v in groups[x])
   if not differs:continue
   estimate=float(format(estimate,'.3g'))
   # Avoid a rounded boundary crossing its unmodified neighboring observation.
   index=xs.index(x)
   lower=fitted[index-1] if index else 0;upper=fitted[index+1] if index+1<len(xs) else math.inf
   estimate=max(lower,min(upper,estimate))
   why=f'{curve["panel"]}/{curve["metric"]}按用户要求的非递减趋势假设进行对数空间保序拟合；非新实测、非作者结果，非严格算法定理。不同参数/电路结构原本可产生真实下降；此处仅为趋势预测情景。重复实际召回点等权合并，缺测位置不外推，不重复乘ANNProof系数。'
   for c,v in groups[x]:
    setvalue(s,r,c,estimate,'预测修订',why,inputs);changed=True
   if changed:label_cells.add((sn,curve['label']))
  fits.append(dict(sheet=sn,curve=curve,inputs=inputs,fitted=fitted))
# One more projection resolves rounding at equal fitted block boundaries.
for sn in names:
 s=w[sn]
 for curve in curves:
  cs=[c for c in curve['cols'] if isinstance(s.cell(curve['row'],c).value,(int,float))]
  for a,b in zip(cs,cs[1:]):
   va=s.cell(curve['row'],a).value;vb=s.cell(curve['row'],b).value
   if vb<va-1e-9:
    # Rounding only; retain exact fitted block center rather than accumulating.
    assert abs(vb-va)/max(va,vb)<.005,(sn,curve,va,vb)
    target=a if (sn,s.cell(curve['row'],a).coordinate) in predicted else b
    value=vb if target==a else va
    setvalue(s,curve['row'],target,value,'预测修订','保序拟合预测值的舍入边界校正；非实测。')
for sn,r in label_cells:
 c=w[sn].cell(r,4)
 if c.value and '†含预测' not in str(c.value):c.value=str(c.value)+' †含预测'
for sn in names:
 w[sn]['A1']=str(w[sn]['A1'].value or '')+' | †预测：橙色红字为趋势预测修订，非实测；绿色为已有配置重选。原始数据见备份及趋势修订记录。'
 w[sn]['A1'].alignment=Alignment(wrap_text=True);w[sn].row_dimensions[1].height=45
def add(title,header,rows):
 s=w.create_sheet(title);s.append(header)
 for row in rows:s.append(row)
 s.freeze_panes='A2';s.auto_filter.ref=s.dimensions
 for c in s[1]:c.font=Font(bold=True)
 for col in s.columns:s.column_dimensions[col[0].column_letter].width=24 if col[0].column<7 else 65
 for row in s:
  for c in row:c.alignment=Alignment(wrap_text=True,vertical='top')
add('趋势预测修订说明',['项目','说明'],[
 ['性质','用户要求的单调趋势预测情景，不是新增实测。原始曲线跨配置时不一定理论单调，不能用修订后趋势证明算法性质。'],
 ['优先处理','ANNProof低召回高成本配置先以三项性能均优且召回不低的已有配置整组替换，实际召回同步更新。'],
 ['残余下降','对四个主表的Recall、K、Dataset size曲线，在非递减假设下作对数空间等权保序拟合。重复实际召回等权去重；预测修订3位有效数字。'],
 ['标记','橙色红字、数字后†预测、方法名†含预测、单元格批注。绿色为既有测量配置重选；不是新测量。'],
 ['缺项','空白、超资源范围和不适用位置保持空白，无外推。索引大小、构建时间及默认比较行未变。'],
 ['系数','ANNProof原有query×1.2、VO×2、verify×1.5保留，不重复调整。'],
 ['历史记录','旧说明页与原始实测附表保留历史含义，主表新预测以本页和趋势预测修订记录为准。'],
 ['备份',str(BACKUP)],['拟合输入','同目录fit-inputs.json；每个修订单元格批注也记录输入。']])
add('趋势预测修订记录',['数据集','单元格','修订前','修订后','性质','依据','来源/拟合输入'],[[e['sheet'],e['cell'],e['before'],e['after'],e['kind'],e['reason'],json.dumps(e['source'],ensure_ascii=False)] for e in events])
finalmap={(e['sheet'],e['cell']):e['after'] for e in events}
checks=0
for sn in names:
 s=w[sn]
 for curve in curves:
  vals=[s.cell(curve['row'],c).value for c in curve['cols'] if isinstance(s.cell(curve['row'],c).value,(int,float))]
  assert all(b>=a-1e-8 for a,b in zip(vals,vals[1:])),(sn,curve,vals)
  checks+=1
 for r in [21,23,25,27,35,37,39,41,49,51,53,55]:
  vals=[s.cell(r,c).value for c in range(6,26) if isinstance(s.cell(r,c).value,(int,float))]
  assert all(b>=a-1e-8 for a,b in zip(vals,vals[1:])),(sn,r)
for os in original:
 for row in os:
  for c in row:
   key=(os.title,c.coordinate)
   if key in finalmap or key==(os.title,'A1') and os.title in names or c.column==4 and (os.title,c.row) in label_cells:continue
   assert c.value==w[os.title][c.coordinate].value,(os.title,c.coordinate)
summary=dict(numeric_cells_revised=len(finalmap),prediction_cells=len(predicted),reselected_cells=len(selected),curve_checks=checks,prediction_curve_groups=len(fits),sift40={str(r):w['SIFT1M'].cell(r,11).value for r in [23,24,38,52]},backup_sha256=sha(BACKUP),applied=args.apply)
(S/'fit-inputs.json').write_text(json.dumps(fits,ensure_ascii=False,indent=2),encoding='utf-8')
(S/'changes.json').write_text(json.dumps(events,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False))
if args.apply:
 assert sha(SOURCE)==sha(BACKUP)
 tmp=S/'work9_trend_revision.xlsx';w.save(tmp)
 reloaded=openpyxl.load_workbook(tmp)
 for (sn,c),v in finalmap.items():assert math.isclose(reloaded[sn][c].value,v,rel_tol=1e-13,abs_tol=1e-10)
 shutil.copy2(tmp,SOURCE);summary['workbook_sha256']=sha(SOURCE)
 (S/'APPLIED.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
