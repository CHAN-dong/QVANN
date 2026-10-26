"""Summarize only workbook-backed measurements; retain pending states."""
from datetime import datetime
from zoneinfo import ZoneInfo
from work9_multidata import *

def main():
    wb=openpyxl.load_workbook(OUTPUT,data_only=True)
    progress=json.loads((STUDY/'workbook_progress.json').read_text())
    final=STUDY/'final_validation.json'
    status=json.loads(final.read_text())['status'] if final.exists() else 'IN PROGRESS'
    lines=['# 新增三个数据集实验进度与结果','',f'状态：{status}；生成时间：{datetime.now(ZoneInfo("Asia/Shanghai")).isoformat()}。',
           f'已填实测单元格：{progress["measured_cells"]}/873；不适用单元格：54。','',
           '快速预览：新增数据统一 M=16、efConstruction=200。20 条独立查询，重复3次；单线程测量。',
           '原 SIFT 工作表保留。GloVe 使用完整1183514条归一化向量；GIST/DEEP使用1M。',
           '规模保留100K/500K/1M/5M/10M，5M和10M按用户选择标为不适用。',
           '时间单位ms；VO单位KB=1000 bytes；Query包含真实VO生成；Verify包含解码、认证和重放。','']
    def fmt(value):return f'{value:.3f}' if isinstance(value,(int,float)) else str(value)
    for name,n,d in DATASETS.values():
        ws=wb[name]
        lines +=[f'## {name} (N={n:,}, d={d})','', '| 方案 | 总体Recall % | Query ms | Verify ms | VO KB | 曲线末端Recall % | Query ms | Verify ms | VO KB |', '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
        for method,row in [('VLAH',9),('ANNProof',10),('Ours',11)]:
            pairs=CURVE_ROWS[method];rec=pairs[0][0]
            values=[ws.cell(row,c).value for c in [5,6,8,7]]+[ws.cell(rec,16).value,ws.cell(pairs[2][1],16).value,ws.cell(pairs[1][1],16).value,ws.cell(pairs[0][1],16).value]
            lines.append('| '+method+' | '+' | '.join(map(fmt,values))+' |')
        lines+=['']
    lines+=['参数和原始CSV位置记录于工作簿批注、测量记录及单元格来源页。',
            '召回率档位在同一查询工作负载上选择，非独立留出召回估计；表格保留实际召回率，不插值。',
            'DEEP/ANNProof 的最小合法 efSearch=10 已达71%召回率，60/65/70%阈值共用该实测点；实际起点受参数下限影响。',
            'M8/efc100 的 GIST 探索索引存在入口不可达节点，旧扫描和连通性诊断已保留，未作为最终数值。',
            'ANNProof/VLAH保留算法重实现范围，不宣称作者代码或链上系统的严格数值复现。']
    (STUDY/'RESULTS_CN.md').write_text('\n'.join(lines)+'\n')
    print(json.dumps(dict(status=status,report=str(STUDY/'RESULTS_CN.md'),measured_cells=progress['measured_cells'])))

if __name__=='__main__':main()
