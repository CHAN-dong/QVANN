from pathlib import Path
import json,openpyxl
ROOT=Path(__file__).resolve().parents[1]
w=openpyxl.load_workbook(ROOT.parent/'work9.xlsx')
for sheet in ['SIFT1M','NYTimes-256','GloVe-100','DEEP1M']:
 s=w[sheet];print('\nSHEET',sheet)
 for row in s:
  for c in row:
   if c.column==4 and c.value and 'ANNProof' in str(c.value):
    print('METHOD',c.row,[(x.coordinate,x.value) for x in s[c.row] if x.column>=5 and x.value is not None])
    if s.cell(c.row,5).value=='Recall rate':print('METRIC',[(x.coordinate,x.value) for x in s[c.row+1] if x.column>=5 and x.value is not None])
for f in sorted((ROOT/'experiments').glob('work9*/pilot-points-*ANNProof*.json')):
 r=json.loads(f.read_text());print('PILOT',f.name,'floor',min(x['recall'] for x in r if x['valid']),'max',max(x['recall'] for x in r if x['valid']))
