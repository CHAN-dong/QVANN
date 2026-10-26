"""Finalize reports from actual recorded ArXiv experiments, never infer missing costs."""
import csv,json,platform
from pathlib import Path
import run_openai_arxiv_20261024 as a
import work9_resource_matched as u
S=a.S;R=a.R;O=a.O
a.report()
for method in ['ANNProof','Ours','VLAH']:
    metadata=O/'indexes'/method/'environment.json'
    if metadata.exists():
        data=u.read(metadata);data.update(threads=8,offline_build_threads=8,query_threads=None,scope='Index-only helper; no timed queries. The generic launcher originally recorded its query default1; native offline.json records the actual build threads8.')
        u.write(metadata,data)
overall=u.read(S/'overall.json');ab=u.read(S/'table4.json');curves=u.read(S/'recall_curves.json');protocol=u.read(S/'protocol.json')
protocol['hash_implementations']=dict(ANNProof='Native Keccak-256',VLAH='SHA-256',QVANN='SHA-256',Table4='SHA-256',MR_tree='SHA-256 and Ed25519',V3DB='Native Plonky2 proof and Merkle commitment implementation')
if (S/'V3DB-tuning-finished.json').exists():
    protocol['V3DB_PQ_tuning']=u.read(S/'V3DB-tuning-finished.json')
    final=next(x for x in overall if x['method']=='V3DB');cfg=final.get('config',{})
    if cfg:
        protocol['V3DB_initial_model']='openai_arxiv1m-1000000-M128';protocol['V3DB_final_model']=final['model'];protocol['defaults']=protocol['defaults'].replace('M128,K256,nlist1024',f"M{cfg['M']},K{cfg['K']},nlist{cfg['n_list']}")
u.write(S/'protocol.json',protocol)
def table(data):
    out=['| Method | Status | R (%) | Q (ms) | VO (KB) | V (ms) |','|---|---|---:|---:|---:|---:|']
    for r in data:
        f=lambda k: f'{r[k]*(100 if k=="recall" else 1):.2f}' if r.get('status')=='measured' and k in r else '—'
        out.append('| '+' | '.join([r['method'],r.get('status','pending')]+[f(k) for k in ['recall','query_ms','vo_kb','verify_ms']])+' |')
    return out
text=['# OpenAI-ArXiv-1M: measured overall, recall-cost curves and Table4','',
 'Source: Big ANN OpenAI-ArXiv, first1,000,000 base rows,1536 original float32 dimensions, all20,000 source query vectors. No normalization or dimension reduction. Six independently re-downloaded source ranges match prepared vectors byte-for-byte; none of the20 measurement queries exactly equals a selected base vector. See dataset-audit.json and dataset provenance.','',
 'Query IDs100..119,20 queries ×3 repetitions; k10; query/proof/verification one thread. M16/efConstruction200 shared global graph for ANNProof, QVANN and graph ablations. VLAH has separate native LSH bucket graphs. Exact exhaustive L2 ground truth is recomputed for the1M prefix. Calibration and measurement use the same20 IDs, so reported recall is not a held-out accuracy claim. All data below are unscaled native measurements; KB=1000bytes, not KiB.','',
 '## Overall raw measurements','']+table(overall)+['',
 'Default selection is the smallest tested operating point reaching>=95% Recall, retaining the actual achieved recall. The finite grid can overshoot95%; these defaults are approximately matched, not exactly equal-recall claims. V3DB uses a separately reported lower-recall feasible configuration with one fixed query100 proof; its Recall comes from20 search-only queries and is not the recall of one sample treated as an average. MR-tree performs exact kNN.','',
 '## Table4 raw measurements','']+table(ab)+['',
 'The first two pairs preserve their exact search decisions/results while changing progressive disclosure; the final pair preserves SQ4 traversal and results while changing authentication organization. Every normal packet is checked by the existing VO-only verifier and negative tests. Params, native CSV paths and hashes are in table4.csv/json and records/.','']
for left,right in [(1,3),(2,4),(5,6)]:
    x=next(r for r in ab if r.get('variant')==left);y=next(r for r in ab if r.get('variant')==right)
    if x.get('status')==y.get('status')=='measured':
        delta={k:(1-y[k]/x[k])*100 for k in ['query_ms','vo_kb','verify_ms']}
        text += [f"{x['method']} → {y['method']}: measured Q/VO/V reductions {delta['query_ms']:.2f}% / {delta['vo_kb']:.2f}% / {delta['verify_ms']:.2f}%. A negative reduction is a regression and is retained.",'']
text += ['## Recall curves and limits','']
for method in ['Ours','ANNProof','VLAH','V3DB']:
    points=[r for r in curves if r['method']==method and r.get('status')=='measured'];unique={(r.get('model',''),r['ef'],r['probes']):r for r in points};missing=[r['target'] for r in curves if r['method']==method and r.get('status')!='measured']
    if unique:text += [f"{method}: {len(unique)} distinct actual parameter settings, measured Recall range {min(r['recall'] for r in points)*100:.2f}%–{max(r['recall'] for r in points)*100:.2f}%. Missing target columns: {missing}.",'']
    else:text += [f'{method}: no verified online point available; check the status/limit records.','']
text += ['Targets in the worksheet are requests, not substituted x coordinates. Every curve recall row contains actual recall; multiple targets can map to the same tested point. The chart sheet deduplicates these points and uses actual Recall on x. ANNProof cannot be claimed to reach40% merely because its first measured point is used in the40% target column. Gaps mean not measured/reached under this declared grid and budget, not a proof of theoretical impossibility. MR-tree supplies only the exact100% endpoint.','',
 'V3DB additionally considers a finer PQ family because the initial M128/K256 model has only41% full-probe Recall. Shape-only gate preflights select the largest feasible tested K for M256 before accuracy/timing are observed; a configuration is measured only if its real proof completes. This changes PQ parameters, not just n_probe; model/M/K are explicitly retained in CSVs and chart parameter cells. Original M128 measurements are retained. The final default selects highest actual recall among completed proof configurations. See V3DB-tuning-finished.json for the actual outcome. This is a limited tuning study, not a proof of globally optimal PQ configuration.','',
 '## Coefficients and workbook layout','',
 'work9.xlsx adds OpenAI-ArXiv-1M, ArXiv原始实测, ArXiv实测与来源 and ArXivRecall曲线. The main comparison/curves retain the prior explicitly derived ANNProof* display factors Q×1.2,VO×2,V×1.5; Recall and indexes are not scaled. Raw native values remain in the raw sheet and all primary CSVs. overall.tex is raw; overall_adjusted.tex is the explicitly labeled coefficient display. Table4 is raw, with no inherited ANNProof or NSW multiplier. No smoothing/monotonic correction/prediction is applied to measured results.','',
 '## Offline construction and resource scope','',
 'Index size and construction time are independent actual builds and serving snapshots, not query-benchmark cache load times. Native ADS graph builds use up to8 offline threads; MR-tree construction is serial; V3DB original training uses16 coarse Faiss threads and8 single-thread PQ workers. Do not compare these construction times with older single-thread rows without matching resources. The actual host is recorded in actual-environment.json; it is not an unmeasured512GB machine. Proof processes use the existing50GiB address-space cap.','']
text += ['Native hash implementations are retained: ANNProof uses Keccak-256, while the other ADS implementations and Table4 use SHA-256 (MR-tree also signs its root with Ed25519). Their measured verification times include this implementation difference; this experiment does not claim a uniform SHA-256 microbenchmark.','']
text += ['Construction timing ends after the search index/model and authentication structures are built; the subsequent serving.snapshot export is excluded. Native Faiss graph builders write their graph cache within the build timer, whereas MR-tree serializes its serving snapshot separately. Consequently these are native construction scopes, not a uniform end-to-end durable-index I/O benchmark. Snapshot export wall time is not query latency.','']
for r in overall:
    if 'index_mb' in r:text += [f"{r['method']}: actual serving index {r['index_mb']:.2f}MB, measured construction {r['index_seconds']:.2f}s. See its index provenance for exact components and thread settings.",'']
text += ['V3DB static gate limits are declared experiment budgets. A budget stop must not be presented as an intrinsic RAM lower bound or as evidence that512GB cannot run that shape. The gate counts, actual attempts and raw failure logs are preserved; missing higher-recall costs are not forecast. A successful gate-only preflight is not a successful query/proof measurement.','',
 '## Reproduction and integrity','',
 'See README.md for preparation, run/resume and audit commands. Query native implementations and old experiment/workbook data are retained. Independent audits check source bytes, paired ablation results, raw-to-display factors, the complete1536D QVANN wire protocol and all new worksheet cells. The original workbook is backed up before modification.','']
(S/'REPORT.md').write_text('\n'.join(text),encoding='utf-8')
zh=['# OpenAI-ArXiv-1M 实验说明','','使用原始 1536 维向量的前 100 万条，不降维、不重新归一化；使用原查询集的第 100–119 条查询，k=10，20 条×3 次，查询及验证单线程。V3DB 每个完成的配置仅对固定 query100 出证一次，其召回率由 20 条检索查询计算。','','## 原始 Overall','','']+table(overall)+['','## 原始 Table 4','','']+table(ab)+['','除 V3DB 外，默认点选择实测 Recall≥95% 的最小已测预算，实际 Recall 并不完全相同，不能声称严格等召回率。召回率曲线使用实际召回率；目标列是参数选择目标，不能替代实测横坐标。','','主工作表与 overall_adjusted.tex 沿用历史 ANNProof* 展示系数：Q×1.2、VO×2、V×1.5。这些是明确标注的派生值；以上表格、原始实测页、overall.tex、Table 4 及主要 CSV 均保留原始测量。未进行单调平滑或预测补值。','','新数据集的图索引离线构建采用 8 线程，MR-tree 为串行；V3DB 训练采用原作者脚本的 16 线程粗聚类和 8 个单线程 PQ 工作进程。不能直接把构建时间与历史单线程测量合并比较。构建计时与后续索引快照导出分开，具体范围见 REPORT.md。','','Table 4 中 NSW-ADS+(i) 虽减少 VO，验证时间却高于 NSW-ADS；保留这一退化。高维下渐进披露、解码与距离边界检查也可能带来明显计算开销。','','V3DB 的高召回率空格表示没有完成相应真实证明，具体预算和尝试见日志；不能据此断言 512 GB 机器也无法运行。本机实际资源记于 actual-environment.json。原有工作簿数据保留，并有修改前备份。','','复现命令见 README.md，完整协议见 protocol.json，最终独立审计见 independent-audit.json。']
(S/'结果说明.md').write_text('\n'.join(zh),encoding='utf-8')
sources=[R/'scripts/prepare_openai_arxiv_20261024.py',R/'scripts/run_openai_arxiv_20261024.py',R/'scripts/audit_openai_arxiv_20261024.py',R/'scripts/tune_v3db_openai_arxiv_20261024.py',Path(__file__).resolve(),R/'build/veriann_lut_benchmark.cpp',R/'src/paper_repro_common.hpp',R/'src/verifiable_ann_common.hpp',R/'src/veriann_query_lut.hpp',R/'src/experiments/work9_ablation.cpp',R/'ANNProof-24FGCS/annproof.cpp',R/'VLAH-25TrustCom/vlah.cpp',R/'Chen13-MR/mr.cpp']
sources.extend(sorted((S/'index-build').glob('*.cpp')))
u.write(S/'final-source-fingerprints.json',{str(p.relative_to(R)):u.sha(p) for p in sources})
print('REPORT_FINALIZED',str(S/'REPORT.md'),flush=True)
