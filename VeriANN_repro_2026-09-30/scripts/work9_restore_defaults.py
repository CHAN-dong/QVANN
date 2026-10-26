"""Restore the user's original 95%-default methods after the scope correction."""
from copy import copy
import csv,json,shutil,time
from pathlib import Path
import openpyxl
from openpyxl.comments import Comment
from work9_resource_matched import ROOT,STUDY,SOURCE,BACKUP,DATA,sha,write

stamp=time.strftime('%Y%m%d_%H%M%S')
shutil.copy2(SOURCE,STUDY/f'work9_before_scope_correction_{stamp}.xlsx')
wb=openpyxl.load_workbook(BACKUP)
changes=[]
# Restore the ENTIRE pre-task workbook first, including all other methods,
# original curves, index measurements, k/size panels and controlled ablations.
# Only the V3DB cells below differ from that backup.
for ds,cfg in DATA.items():
 ws=wb[cfg['sheet']]
 ws['A4']='VLAH、ANNProof、Ours维持原默认约95%召回率及原实测数据。只有V3DB采用本机可运行的低Recall@10默认配置。低召回率扩展仅用于Recall@10曲线；不同默认召回率不视为等质量比较。'
 ah=next(c.row for row in ws for c in row if c.column==6 and c.row>120 and c.value=='Recall@10')
 # Withdraw previous high-recall V3DB forecasts from current comparison cells.
 for r,cols in [(12,range(5,9)),(ah+7,range(6,10))]+[(r,range(6,17)) for r in [26,27,38,39,50,51]]+[(r,range(6,11)) for r in [64,75,85]]+[(r,range(6,12)) for r in [97,108,118]]:
  for col in cols:
   cell=ws.cell(r,col);cell.value='待V3DB实测';cell.comment=Comment('仅V3DB改用可运行低召回默认配置；旧预测撤出当前对比，其他方案数据已从原备份完整恢复。','口径纠正')
   cell._style=copy(ws.cell(11,6)._style)
 ws['D12']='V3DB';ws.cell(ah+7,4,'V3DB baseline')
 for r in [26,38,50,64,75,85,97,108,118]:ws.cell(r,4,'V3DB')
 f=STUDY/'records'/f'{ds}-V3DB-overall.json'
 if not f.exists():continue
 rec=json.loads(f.read_text())
 if rec.get('status')!='passed' or rec.get('samples')!=60:continue
 rows=list(csv.DictReader((Path(rec['output'])/'per_query.csv').open()))
 assert len(rows)==60 and all(r['verified']=='1' for r in rows)
 for r,cols in [(12,[(5,'recall',100),(6,'query_ms',1),(7,'vo_kb',1),(8,'verify_ms',1)]),(ah+7,[(6,'recall',1),(7,'query_ms',1),(8,'vo_kb',1),(9,'verify_ms',1)])]:
  for col,k,scale in cols:
   ws.cell(r,col,rec[k]*scale).comment=Comment(json.dumps(rec,ensure_ascii=False),'V3DB实测来源');changes.append([ws.title,ws.cell(r,col).coordinate,rec[k]*scale])
 for r,k in [(97,'vo_kb'),(108,'verify_ms'),(118,'query_ms')]:
  ws.cell(r,8,rec[k]).comment=Comment(json.dumps(rec,ensure_ascii=False),'V3DB实测来源');changes.append([ws.title,ws.cell(r,8).coordinate,rec[k]])
 # Keep original curve headers/layout; insert only complete proof measurements
 # for V3DB where the existing target is met by an actually measured point.
 for col in range(6,17):
  target=ws.cell(16,col).value/100
  for r,key in [(26,'vo_kb'),(38,'verify_ms'),(50,'query_ms')]:
   if target<=rec['recall']+1e-12:
    for rr,value in [(r,rec['recall']*100),(r+1,rec[key])]:
     ws.cell(rr,col,value).comment=Comment('当前已完成的V3DB配置；更多低预算曲线点仍待实测。\n'+json.dumps(rec,ensure_ascii=False),'V3DB实测来源');changes.append([ws.title,ws.cell(rr,col).coordinate,value])

note=wb.create_sheet('默认口径纠正')
for row in [
 ['项目','纠正后的口径'],['其他方案','VLAH、ANNProof、Ours默认约95%召回率。四个数据集原有整体、索引、规模、K、消融及原召回曲线数据均从本次操作前备份恢复。'],
 ['V3DB','只有V3DB采用可运行的低Recall@10默认值；仅完整60次验证通过的实测填入。尚未测完的位置保留文字状态，不再用高召回预测。'],
 ['低召回扩展','仅适用于Recall@10 vs. VO/Verify/Query曲线，不改变其他方案的默认配置。错误低召回默认实验已停止，数据归档，不进入当前默认表。'],
 ['恢复来源',str(BACKUP)],['恢复校验','逐单元格核对所有非V3DB数据：值、格式、批注、合并区域应与恢复来源一致。'],['时间',stamp]]:note.append(row)
note.column_dimensions['A'].width=20;note.column_dimensions['B'].width=130
output=STUDY/'work9_defaults_restored.xlsx';wb.save(output)

# Independently ensure ALL non-V3DB cells are restored, including annotations.
base=openpyxl.load_workbook(BACKUP);restored=openpyxl.load_workbook(output)
checks=0
for ds,cfg in DATA.items():
 old=base[cfg['sheet']];new=restored[cfg['sheet']]
 ah=next(c.row for row in old for c in row if c.column==6 and c.row>120 and c.value=='Recall@10')
 excluded={12,26,27,38,39,50,51,64,75,85,97,108,118,ah+7}
 assert set(map(str,old.merged_cells.ranges))==set(map(str,new.merged_cells.ranges))
 for row in old:
  for a in row:
   if a.row in excluded or a.coordinate=='A4':continue
   b=new[a.coordinate]
   assert a.value==b.value,(old.title,a.coordinate,a.value,b.value)
   assert a._style==b._style,(old.title,a.coordinate,'style changed')
   assert (a.comment.text if a.comment else None)==(b.comment.text if b.comment else None),(old.title,a.coordinate,'comment changed')
   checks+=1
shutil.copy2(output,SOURCE)
assert sha(SOURCE)==sha(output)
write(STUDY/'SCOPE_CORRECTED.json',dict(status='original non-V3DB data restored and independently checked',non_v3db_cells_checked=checks,source_backup=str(BACKUP),workbook=str(SOURCE),sha256=sha(SOURCE),background_runner='stopped; obsolete resource-matched default writer must not restart',preserved_v3db_cells=changes))
print(json.dumps(dict(status='RESTORED',checked_cells=checks,workbook=str(SOURCE),sha256=sha(SOURCE)),ensure_ascii=False))
