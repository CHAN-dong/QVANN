"""Add QVANN's measured default refinement row; change display precision only."""
import csv
import json
import math
import shutil
import statistics
import struct
from copy import copy
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path

import openpyxl
from openpyxl.comments import Comment
from openpyxl.styles import Font
import work9_resource_matched as u

R=u.ROOT;S=R/'experiments/work9-vo-breakdown-20261023';Q=R/'experiments/work9-quantization-20261023'
SOURCE=R.parent/'work9.xlsx';BACKUP=S/'work9_before_qvann_row.xlsx'
assert not (S/'QVANN_APPLIED.json').exists(),'Already applied'
if not BACKUP.exists():shutil.copy2(SOURCE,BACKUP)
assert u.sha(SOURCE)==u.sha(BACKUP)
proto=u.read(Q/'protocol.json');old=u.read(S/'summary.json');previous=u.read(S/'APPLIED.json')
records=list(old);ours=[];audit=[]

def fmt(value):
    x=Decimal(str(value));one=x.quantize(Decimal('.1'),rounding=ROUND_HALF_UP)
    if abs(one)>=1000:return str(x.quantize(Decimal('1'),rounding=ROUND_HALF_UP))
    return format(one,'f').rstrip('0').rstrip('.') if '.' in format(one,'f') else str(one)

for ds,cfg in proto['datasets'].items():
    path=Path(cfg['source'])/'per_query.csv'
    rows=[r for r in csv.DictReader(path.open()) if int(r['k'])==10 and int(r['ef_search'])==cfg['E'] and 100<=int(r['query'])<120]
    assert len(rows)==60
    fresh=R/'results/work9-quantization-20261023'/ds
    quant={(int(r['query']),int(r['repeat'])):r for r in csv.DictReader((fresh/'samples.csv').open()) if r['configuration']=='SQ4--SQ6--raw'}
    assert len(quant)==60
    for row in rows:
        assert row['verified']=='1'
        assert int(row['record_bytes'])+int(row['local_proof_bytes'])+int(row['route_proof_bytes'])==int(row['vo_bytes'])
        q=quant[int(row['query']),int(row['repeat'])]
        assert int(row['vo_bytes'])==int(q['vo_bytes']) and math.isclose(float(row['recall_at_k']),float(q['recall']),abs_tol=1e-12)
    # Independently parse the retained SQ4--SQ6--raw wire, including all counts.
    wire=fresh/'configuration1.vo';data=wire.read_bytes();pos=0;d=cfg['manifest'].get('dimension',{'sift1m':128,'nytimes256':256,'glove100':100,'deep1m':96}[ds])
    def read():
        global pos
        val=struct.unpack_from('<I',data,pos)[0];pos+=4;return val
    def skip(n):
        global pos
        pos+=n;assert pos<=len(data)
    assert read()==0x56505231;skip(32);assert read()==10 and read()==cfg['E'] and read()==10;skip(40)
    skip(read()*(4+d//2))
    for _ in range(read()):skip(12);skip(read()*4)
    skip(read()*(8+d//4));skip(read()*(8+d*4));payload_end=pos
    for _ in range(3):skip(read()*32)
    assert pos==len(data)
    first=next(r for r in rows if int(r['query'])==100 and int(r['repeat'])==0)
    assert payload_end==int(first['record_bytes']) and len(data)==int(first['vo_bytes'])
    audit.append(dict(dataset=ds,file=str(wire),sha256=u.sha(wire),record_bytes=payload_end,proof_bytes=len(data)-payload_end))
    t=statistics.fmean(int(r['record_bytes'])-32 for r in rows)
    a=statistics.fmean(int(r['local_proof_bytes'])+int(r['route_proof_bytes'])+32 for r in rows)
    total=statistics.fmean(int(r['vo_bytes']) for r in rows)
    assert math.isclose(t+a,total,abs_tol=1e-8)
    rec=dict(dataset=ds,sheet=cfg['sheet'],method='QVANN (Ours)',N=cfg['manifest']['base'],D=d,parameter=f"E={cfg['E']}; SQ4--SQ6--raw",samples=60,independent_queries=20,source=str(path),csv_sha256=u.sha(path),factor=1,status='measured VO byte disaggregation',T_raw_bytes=t,A_raw_bytes=a,total_raw_bytes=total,T_display_bytes=t,A_display_bytes=a,total_display_bytes=total,recall=statistics.fmean(float(r['recall_at_k']) for r in rows))
    records.append(rec);ours.append(rec)

for variant in ['raw','adjusted']:
    path=S/f'vo_breakdown_{variant}.tex';text=path.read_text();backup=S/f'vo_breakdown_{variant}_before_qvann.tex'
    if not backup.exists():shutil.copy2(path,backup)
    lines=text.splitlines();result=[]
    for line in lines:
        if line.startswith(r'\textbf{\textsf{QVANN}'):continue
        if line.startswith('\\resizebox'):line='% '+line
        elif line=='}':line='% }'
        elif line.startswith('Result-only'):
            for v in ['5.16','10.28','4.04','3.88']:line=line.replace('{'+v+'}','{'+fmt(v)+'}')
        elif line.startswith('\\textsf{') and ' & ' in line:
            method=line.split('}')[0].split('{')[1]
            numbers=[];prefix='display' if variant=='adjusted' else 'raw'
            for ds in proto['datasets']:
                rec=next(r for r in records if r['dataset']==ds and r['method']==method)
                numbers.extend(fmt(rec[x+'_'+prefix+'_bytes']/1000) for x in ['T','A'])
            line=line.split(' & ')[0]+' & '+' & '.join(numbers)+r' \\'
        elif line==r'\midrule' and result and result[-1].startswith('Result-only'):line=r'\hline'
        elif line==r'\bottomrule':
            numbers=[]
            for rec in ours:numbers.extend([fmt(rec['T_raw_bytes']/1000),fmt(rec['A_raw_bytes']/1000)])
            result.append(r'\textbf{\textsf{QVANN} (Ours)} & '+' & '.join(numbers)+r' \\')
        result.append(line)
    path.write_text('\n'.join(result)+'\n')
    for line in result:
        if ' & ' in line and (line.startswith(r'\textsf{') or line.startswith(r'\textbf{')):
            for number in line.removesuffix(r' \\').split(' & ')[1:]:
                assert '.' not in number or len(number.split('.')[1])<=1
                assert '.' not in number if abs(Decimal(number))>=1000 else True

wb=openpyxl.load_workbook(BACKUP);allowed=set()
for rec in ours:
    ws=wb[rec['sheet']];loc=previous['new_tables'][rec['dataset']];row=loc['last_row']+1
    assert all(ws.cell(row,c).value is None for c in range(4,12))
    vals=[rec['method'],rec['recall'],rec['T_raw_bytes']/1000,rec['A_raw_bytes']/1000,rec['total_raw_bytes']/1000,rec['T_raw_bytes']/1000,rec['A_raw_bytes']/1000,1]
    for col,val in enumerate(vals,4):
        cell=ws.cell(row,col,val);cell._style=copy(ws.cell(row-1,col)._style);cell.font=Font(bold=True)
        cell.comment=Comment('QVANN (Ours)，默认SQ4--SQ6--raw；实际VO拆分，20查询×3。\nT=record_bytes-32；A=local_proof_bytes+route_proof_bytes+32；32字节为query绑定哈希。\n'
                             f"N={rec['N']}，{rec['parameter']}。\n来源={rec['source']}\nSHA256={rec['csv_sha256']}\n与本轮量化配置实测的默认组逐条VO长度和召回一致；未乘任何系数。",'Codex')
        allowed.add((ws.title,cell.coordinate))
    for rr in range(loc['header_row']+1,row+1):
        for col in range(6,11):
            cell=ws.cell(rr,col)
            if isinstance(cell.value,(int,float)):
                cell.number_format='0' if abs(Decimal(fmt(cell.value)))>=1000 else '0.0'
                allowed.add((ws.title,cell.coordinate))
    previous['new_tables'][rec['dataset']]['qvann_row']=row
details=wb['VO拆分原始与系数'];headers=[c.value for c in details[1]]
for rec in ours:details.append([rec[k] for k in headers])
notes=wb['VO拆分实验说明'];notes.append(['QVANN补充','2026-10-23：新增四数据集QVANN默认SQ4--SQ6--raw拆分，逐条匹配量化实验默认组VO长度；共24组、1440条样本。论文表最多一位小数，四位及以上整数不显示小数；原始精度保留。'])
review=S/'work9_vo_breakdown_with_qvann.xlsx';wb.save(review)
oldbook=openpyxl.load_workbook(BACKUP);newbook=openpyxl.load_workbook(review);preserved=0
for ws in oldbook:
    for row in ws:
        for cell in row:
            new=newbook[ws.title][cell.coordinate]
            assert cell.value==new.value,(ws.title,cell.coordinate,'old value changed')
            assert (cell.comment.text if cell.comment else None)==(new.comment.text if new.comment else None)
            if (ws.title,cell.coordinate) not in allowed:assert cell.number_format==new.number_format
            preserved+=1
u.write(S/'summary-with-qvann.json',records);u.write(S/'qvann-raw-breakdown.json',ours)
check=dict(passed=True,qvann_samples=240,independent_wire_checks=audit,original_values_and_comments_preserved=preserved,workbook_sha256=u.sha(review),backup_sha256=u.sha(BACKUP),tables=previous['new_tables'])
u.write(S/'qvann-audit.json',check)
assert u.sha(SOURCE)==u.sha(BACKUP)
try:shutil.copy2(review,SOURCE)
except PermissionError:print('AUDITED_REVIEW_READY_SOURCE_LOCKED',flush=True)
else:
    assert u.sha(SOURCE)==check['workbook_sha256'];u.write(S/'QVANN_APPLIED.json',check);print('APPLIED',flush=True)
for rec in ours:print(rec['dataset'],fmt(rec['T_raw_bytes']/1000),fmt(rec['A_raw_bytes']/1000))
