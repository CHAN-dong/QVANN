import json
from pathlib import Path
import openpyxl
from work9_resource_matched import ROOT,DATA
w=openpyxl.load_workbook(ROOT.parent/'work9.xlsx')
for ds,c in DATA.items():
 s=w[c['sheet']]
 print('MAIN',ds)
 for r in [12,26,27,38,39,50,51,64,75,85,97,108,118]:
  print(r,[(x.coordinate,x.value) for x in s[r] if x.column>=5 and x.value is not None and (r in [12,64,75,85] or isinstance(x.value,str))])
print('CALIBRATION AND MEMORY SHAPES')
for f in sorted((ROOT/'V3DB-26VLDB/work9-cache').glob('*/calibration.json')):
 x=json.loads(f.read_text());c=x['model']['config']
 if c['dataset'] not in DATA or c['N']!=DATA[c['dataset']]['N'] or c['source']!=c['dataset']:continue
 print(f.parent.name,[(p['n_probe'],round(p['recall'],4)) for p in x['points'] if p['k']==10])
