import json,math,csv
from pathlib import Path
import openpyxl
ROOT=Path(__file__).resolve().parents[2]
S=ROOT/'experiments/work9-v3db-reaudit-20261018'
O=ROOT/'experiments/work9-v3db-estimates-20261017'
cells=json.loads((S/'forecast-cells.json').read_text());points=json.loads((S/'forecast-points.json').read_text())
w=openpyxl.load_workbook(ROOT.parent/'work9.xlsx')
previous={r['dataset']:r for r in json.loads((O/'overall-forecast-summary.json').read_text())}
now=json.loads((S/'overall-forecast-summary.json').read_text())
for p in points:
    for key,field in [('query_ms','query_ms'),('verify_ms','verify_ms'),('vo_kb','vo_kb'),('memory_GiB','estimated_peak_memory_GiB')]:
        lo,hi=p['scenario_ranges'][key];assert 0<lo<=p[field]<=hi and math.isfinite(hi),(p['id'],key)
for r in cells:
    c=w[r['sheet']][r['cell']]
    if r['estimated']:assert '[预测]' in c.number_format and c.comment
assert len(cells)==428 and sum(r['estimated'] for r in cells)==255
for ds in ['SIFT1M','NYTimes-256','GloVe-100','DEEP1M']:
    b=openpyxl.load_workbook(S/'work9_immediately_before_write.xlsx')[ds]
    for row in range(9,13):
        for col in ['I','J']:assert w[ds][f'{col}{row}'].value==b[f'{col}{row}'].value
lines=['# V3DB 全部预测复核（2026-10-18）','',
       '固定既有参数、单线程和完整Query计时口径；重算255个预测单元格，保留完整20×3实测值。未发现秒/毫秒换算或重复累计证明时间的错误。',
       '', '新增4个各1次的真实证明校准点（不是完整20×3实验），覆盖计算域bit18、19；结合原有bit20的179条样本重新标定。七个配置上的重构误差约在±5.5%内，但这不验证更高域档位的外推。',
       '', '| 数据集 | 原Query预测（分钟） | 复核Query预测（分钟） | 比值 |','|---|---:|---:|---:|']
summary=[]
for r in now:
    a=previous[r['dataset']];ratio=r['query_ms']/a['query_ms']
    lines.append(f"| {r['dataset']} | {a['query_ms']/60000:.2f} | {r['query_ms']/60000:.2f} | {ratio:.4f} |")
    summary.append(dict(dataset=r['dataset'],old_query_ms=a['query_ms'],new_query_ms=r['query_ms'],ratio=ratio,new_verify_ms=r['verify_ms'],new_vo_kb=r['vo_kb']))
lines+=['','结论：新证据不支持把既有高召回、大电路的预测整体大幅下调；同参数中心估计略升。较小/中间域的Verify模型已改为实测档位插值。大配置仍是充分内存且无换页条件下的估算，不能视为本机完成时间或实测加速比。',
        '', '门数复核：NYTimes的p1/p2、GloVe的p1/p2已直接计数；更大形状在12GiB及40GiB地址空间预算下未完成计数。失败未被当作成功门数或耗时样本。门数拟合仅说明已测范围的吻合，不能替代大形状实测。',
        '', 'SIFT备选：现有搜索校准中M128/K256/p16达到95%，其成本估计约38.7分钟，比主表M128/K1024/p16低；这属于参数优化，而不是同参数公式修正。备选没有替换主表，以免与已实测索引指标配置混用。其他备选见工作簿V3DB参数备选。',
        '', '工作簿新增：V3DB复核差异、V3DB新增校准、V3DB复核说明、V3DB参数备选。32个索引实测单元格保持原值，其他方案保持原值。',
        '', '预测限制：bit20以上没有新增完整证明；部分规模仍只有参数假设且未校准召回。敏感性区间不是置信区间。']
(S/'RESULTS.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
(S/'comparison-summary.json').write_text(json.dumps(summary,indent=2))
(S/'independent-validation.json').write_text(json.dumps(dict(status='PASS',reviewed_cells=428,predicted_cells=255,validated_points=len(points),index_measurement_cells_unchanged=32),indent=2))
print(json.dumps(summary,indent=2));print('PASS: numerical ranges, 255 labels, 428 mappings, 32 index cells.')
