# ANNProof low-recall measurements and explicit coefficient adjustments

The user confirmed the destination is the outer project's `work9.xlsx`.
The source workbook is backed up as `work9_before_annproof_adjustment.xlsx`.

## Why the previous curve did not reach some low recall values

The existing ANNProof implementation retains `efSearch >= k`. With k=10 and
the previously fixed graphs, the 20-query minimum recalls were SIFT 67.5%,
NYTimes 84%, DEEP 71%, and GloVe 36%. Merely requesting efSearch=1 cannot
lower these floors: the effective value is still 10. Some older target
columns therefore reused the same higher-recall point.

This study varies the existing HNSW build parameters M/efConstruction as well
as efSearch. It preserves the ANNProof search, multiproof generation,
authentication and replay code. It keeps k=10, the complete original base,
full dimensions and the original vectors. Configurations that cannot return
ten results for every evaluation query are excluded, rather than counted as
low-recall operating points. In particular, the tested NYTimes M2/efc20
candidate had no valid complete top-10 setting in its pilot grid.

## Measurements

- Four active datasets; targets 40,45,50,55,60,65,70,75,80 percent.
- Query IDs 100..119, three repeats, one query/proof/verification thread.
  Calibration and evaluation share these 20 queries; this is not an independent
  held-out recall evaluation. All actual recalls are retained.
- Use unchanged default graphs when they reach target..target+2.5 percentage
  points. Otherwise prefer a larger M and the smallest legal efSearch in the
  band, breaking remaining ties in favor of higher efConstruction. Additional
  M4/efc20 and M8/efc80 candidates help avoid unnecessarily expensive very sparse
  graphs. These are sampled configurations, not a claim of exhaustive tuning.
- The low-recall curve can change graph topology; it must not be described as
  an efSearch-only experiment on one fixed index. Default approximately 95%
  configurations and index size/construction measurements remain unchanged.
- Search-only pilots are used only for selection. Final timing, VO bytes and
  verification are obtained from complete native protocol runs, checked against
  the corresponding pilot's recall, visited-node count and expanded-node count.
- Query time includes search + VO generation; verification includes actual
  decoding/authentication/replay. VO is the actual serialized wire, KB=1000 bytes.
  Every selected budget has 60 honest verified samples. Each full native batch
  also checks eight tamper cases and the saved first proof's physical byte size.

## Workbook adjustments

The user-requested factors are **query time ×1.2, VO size ×2, verification
time ×1.5**. They are applied once to all ANNProof performance comparison cells
in the four main sheets, after replacing only the 40--80% curve points by new
raw measurements. Other recalls, index measurements and other methods are
preserved. Historical raw measurement/provenance sheets are preserved as raw
evidence, not rescaled. Values outside the remeasured range derive from the
pre-task workbook, so their original query sample/configuration remains the
one stated in their retained source comments.

Adjusted comparisons are explicitly labeled `ANNProof*`, have yellow cells,
and carry comments recording the raw number and multiplier. The visible method
labels and `ANNProof调整说明` identify these as **derived scenario
values, not raw measured performance**. The workbook separately contains
`ANNProof原始补测`, `ANNProof调整后对比`, and `ANNProof单元格变更`.
Existing experimental CSVs are never modified to manufacture the adjustments.

Candidate runs replaced during selection refinement are retained in
`records-before-refinement/` and the result directories. Supplemental native
batches have separate output directories, so no raw run is overwritten.

## Reproduce (inner project root, WSL)

```sh
/root/.venvs/v3db-work9/bin/python scripts/work9_annproof_low_adjusted.py run
/root/.venvs/v3db-work9/bin/python scripts/work9_annproof_low_adjusted.py refine
/root/.venvs/v3db-work9/bin/python scripts/work9_annproof_low_adjusted.py fill
/root/.venvs/v3db-work9/bin/python scripts/audit_annproof_low_adjusted.py
```

Raw runs: `results/work9-annproof-low-adjusted-20261021/`.
The final `independent-audit.json` checks the raw sample means, hashes, actual
proof bytes, query identities, parameter bindings, exactly-once coefficient
mapping and preservation of all unrelated existing workbook cells.
