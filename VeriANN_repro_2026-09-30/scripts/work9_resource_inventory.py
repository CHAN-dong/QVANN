import json,sys
from pathlib import Path
import openpyxl
root=Path(__file__).resolve().parents[1]
w=openpyxl.load_workbook(root.parent/'work9.xlsx')
print('SHEETS',w.sheetnames)
for name in ['SIFT1M','NYTimes-256','GloVe-100','DEEP1M']:
 s=w[name];print('MAPPING',name,[(c.coordinate,c.value) for row in s for c in row if c.column in [4,5,6] and c.value is not None and (c.row<14 or c.row>120)])
if '--mapping-only' in sys.argv:sys.exit(0)
s=w['SIFT1M']
for row in s:
    if row[0].row>=120:print([(c.coordinate,c.value) for c in row if c.value is not None])
print('V3DB MODELS')
for f in sorted((root/'V3DB-26VLDB/work9-cache').glob('*/calibration.json')):
    x=json.loads(f.read_text());c=x['model']['config']
    if c['dataset']=='gist1m':continue
    print(f.parent.name,'cap',c['capacity'],'points',[(p['n_probe'],round(p['recall'],4)) for p in x['points'] if p['k']==10 and p['n_probe']<=16])
print('ACTIVE')
import subprocess
print(subprocess.run(['pgrep','-af','work9|work9_measure|veriann_lut_benchmark|/build/vlah|/build/annproof'],capture_output=True,text=True).stdout)
