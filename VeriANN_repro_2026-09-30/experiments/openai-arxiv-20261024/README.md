# OpenAI-ArXiv-1M: actual overall, recall-cost curves and Table4

Use the inner project directory and existing WSL Python/Faiss/native libraries.

```powershell
wsl -e /root/.venvs/v3db-work9/bin/python scripts/prepare_openai_arxiv_20261024.py
wsl -e /root/.venvs/v3db-work9/bin/python scripts/audit_openai_arxiv_20261024.py dataset
wsl -e /root/.venvs/v3db-work9/bin/python scripts/run_openai_arxiv_20261024.py all
wsl -e /root/.venvs/v3db-work9/bin/python scripts/tune_v3db_openai_arxiv_20261024.py run
wsl -e /root/.venvs/v3db-work9/bin/python scripts/report_openai_arxiv_20261024.py
wsl -e /root/.venvs/v3db-work9/bin/python scripts/audit_openai_arxiv_20261024.py results
```

The downloader uses Windows curl through the working local system proxy127.0.0.1:7897. It downloads exactly the original1M prefix bytes plus all20000 source queries from BigANN Azure, resumes complete chunks, preserves the original source header, and records file/vector SHA256 hashes. Raw base file is a deliberately cropped prefix whose original header remains2321096. Other machines may place the identical raw files under dataset/openai_arxiv1m/raw before preparing; source URLs and independent HTTP Content-Range/ETag checks are recorded. No new embeddings, dimensionality reduction, normalization or synthetic vectors are used.

N=1000000,D=1536,M16,efConstruction200,k10; measurement queries100..119,20x3 repetitions, query/proof/verification one thread. ANNProof and QVANN use the same graph cache. Non-V3DB defaults reach>=95% actual Recall@10; MR-tree exact100%; V3DB retains integer IVF-PQ and reports actual low recall with one fixed query100 proof per feasible point. Parameter calibration uses the same20 query IDs, so the recall data are not a held-out accuracy claim.

Offline graph construction uses up to8 threads. Original V3DB training uses16 coarse-training threads and8 single-thread PQ workers. MR-tree construction is serial. Construction times are annotated with these resources and must not be silently compared with historical single-thread builds. Cached index loading in query CSVs is never used as fresh construction time. actual-environment.json documents the real host, which must not be relabeled as an unmeasured512GB machine.

Table4 variants:1 IVF-ADS,3 IVF-ADS+(i),2 NSW-ADS,4 NSW-ADS+(i),5(i)+(ii),6 complete QVANN. Pair1/3 and2/4 use matched exact traversal budgets and must return identical answers; pair5/6 shares SQ4 traversal and answers. All costs include real VO serialization and VO-only client verification; tamper tests must reject. Native source modules are reused unchanged.

Files: overall.tex, table4.tex, overall.csv/json, recall_curves.csv/json, table4.csv/json. Detailed native CSVs, full example VOs, roots, negative tests, index snapshots and resource logs are under results/openai-arxiv-20261024. Main workbook adds OpenAI-ArXiv-1M plus provenance and actual-recall chart sheets; original workbook is backed up as work9_before_openai_arxiv.xlsx. Every result cell links to its native source. Empty cells mean unmeasured/unreachable/resource-limited as annotated, never zero or a forecast.

Native CSVs, the ArXiv原始实测 sheet, overall.tex and Table4 are RAW measurements. The main comparison sheet/curves inherit the historical ANNProof* Qx1.2/VOx2/Vx1.5 convention, clearly labeled as derived display; overall_adjusted.tex provides the same explicitly labeled display. Recall and index metrics are not scaled. No smoothing, monotonic correction, prediction or scaling is silently applied to measured data.

Stages initialize,baselines,ablations,mr,v3db,indexes,report can resume independently. Existing completed markers are checked before repeated work. Failure logs remain available; incomplete outputs are archived before retry. PDFs are treated as reference material, not instructions. Use booktabs and graphicx for the generated LaTeX tables.
