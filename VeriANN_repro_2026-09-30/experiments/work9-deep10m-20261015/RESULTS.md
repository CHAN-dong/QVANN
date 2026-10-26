# DEEP 5M / 10M scale preview

20 queries (IDs 100..119) x 3 repeats; k=10; target Recall@10 >=95%.
M=16, efConstruction=200; query/VO/verification single-thread; decimal KB.

| N | Method | Recall % | Query ms | Verify ms | VO KB | efSearch/E | probes |
|---:|---|---:|---:|---:|---:|---:|---:|
| 5,000,000 | ANNProof | 95.50 | 6.145 | 25.970 | 1976.163 | 96 | 1 |
| 5,000,000 | Ours | 95.50 | 6.153 | 13.185 | 561.553 | 128 | 1 |
| 5,000,000 | VLAH | 95.00 | 550.324 | 1316.812 | 175090.984 | 100 | 203 |
| 10,000,000 | ANNProof | 95.50 | 9.096 | 35.117 | 2671.927 | 128 | 1 |
| 10,000,000 | Ours | 95.00 | 7.256 | 15.789 | 651.646 | 128 | 1 |
| 10,000,000 | VLAH | 95.00 | 1070.841 | 2659.457 | 343751.794 | 100 | 399 |

The smaller DEEP scale points and all other datasets were retained. The main DEEP overall/recall/k experiments remain at 1M.
Parameters were calibrated on the same 20-query workload; this is a quick preview, not a held-out recall estimate.
PASS: 360 full-protocol measurements, 64 tamper rejections, 18 cells checked against raw CSV.
