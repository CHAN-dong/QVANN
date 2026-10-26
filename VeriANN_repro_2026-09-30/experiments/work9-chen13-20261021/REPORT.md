# Chen13-MR high-dimensional plaintext adaptation — measured results

Implements the plaintext exact-kNN authentication in paper §5.1.1/Fig.6(a), with spatial bulk loading replacing R*-tree construction. No privacy layer or Power Diagram. These are measurements of our adaptation, not the authors' complete confidential MR/EMR/PD system.

| Dataset | Recall@10 | Query + VO (ms) | VO (MB) | Verify (ms) | Index (MB) | Build (s) |
|---|---:|---:|---:|---:|---:|---:|
| SIFT1M | 100% | 278.252 | 235.214 | 202.148 | 538.911 | 1.055 |
| NYTimes-256 | 100% | 204.621 | 282.959 | 243.191 | 324.604 | 0.463 |
| GloVe-100 | 100% | 468.739 | 466.933 | 390.349 | 500.007 | 1.068 |
| DEEP1M | 100% | 423.304 | 373.924 | 398.218 | 405.599 | 0.851 |

23 distinct measured operating points in 13 fresh native runs; 1380 timed queries; 207 rejected negative cases. Each point uses 20 query IDs × 3 repeats, one thread. Independent exhaustive ground truth is recomputed per N.

K and size sweeps are restricted to SIFT/DEEP. SIFT size sweeps use BIGANN prefixes; the main SIFT1M default is the separate standard dataset. DEEP size=1M/k=10 reuses the identical default measurement.

Only the 100% exact point belongs on a recall plot. Existing approximate-method defaults and V3DB forecasts are preserved. Do not describe these as equal-recall 95% experiments.

Index build time excludes input reading, ground-truth search and final snapshot I/O, consistent with the existing index table. Snapshot I/O and whole-process peak RSS are separately available in the CSV/workbook. No network/disk VO transfer is included in query time.

Raw records, complete serving snapshots, signed proof examples and sample CSVs: `results/work9-chen13-20261021/`. Full independent audit: `independent-audit.json`. Workbook preservation/mappings: `fill-audit.json` and the Chen13 workbook sheets.
