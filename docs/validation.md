# Validation performed

Tested on 2026-10-26 under WSL/Linux: Ubuntu 26.04.1, GCC 15.2.0, CMake 4.2.3,
OpenSSL 3.5.5, OpenBLAS 0.3.32, Python 3.14.4. Data preparation and plotting used
NumPy 2.5.4, h5py 3.16.0 and Matplotlib 3.11.2; `requirements.lock` records the
resolved environment. These are tested versions, not a claim that older supported
systems produce identical timings.

## Checks completed

- Retrieved the pinned Faiss and LightGBM source revisions into separate checkouts.
  Applied the dependency patch and built both shared libraries afresh. No original
  research binaries or Conda include/library paths were used in that build.
- Configured and compiled QVANN and the granularity executable in a new build
  directory against those newly built dependencies.
- Passed the existing CTest quantizer/bounds, multiproof and tamper tests. The
  existing synthetic smoke workload also passed all 60 honest query executions;
  this is an integrity test, not experimental evidence for the paper.
- Reconverted the **full real default base datasets**: SIFT1M, NYTimes289761,
  GloVe1183514 and DEEP1M. Each base payload SHA-256 and complete converted query
  file SHA-256 matched the historical experiment inputs. NYTimes's zero base
  filtering and preserved zero query IDs were checked against the original script.
- Exercised `table3` on a real 5,000-vector SIFT prefix with two queries, one repeat,
  and the explicit diagnostic profile. Both honest VOs passed verification.
- Exercised `figure16` on that real SIFT prefix at M=8,16,32,64: 24 measured trials
  spanning all three variants, in addition to calibration and warmups. The native
  experiment checked equal answers, traces and refinement evidence across variants,
  ER/native wire agreement, and rejection of all 180 negative cases.
- Generated Table 3's diagnostic summary CSV and Figure 16's diagnostic CSV,
  PDF and PNG directly from the retained measured samples. Diagnostic figure output
  is visibly labeled and is not presented as a reproduced paper figure.
- Confirmed that plotting rejects duplicate query/repeat measurements and raw
  evidence with a mismatched SHA-256, using the real measured diagnostic files.

## Remaining limits

- This was a fresh **source/dependency/build-directory** test on an existing OS,
  not a container-isolated OS installation. Docker was unavailable. The manual
  GitHub Actions workflow targets Ubuntu 24.04 but has not been run as part of
  these local checks.
- Full four-dataset paper-scale experiment runs, historical graph identity and
  workstation timing have not been rerun here. The original cached graphs were
  constructed with multiple threads; fixed source seeds do not eliminate schedule
  differences. Exact reuse requires the original graph files with matching hashes.
- External baseline implementations and final parameter manifests remain necessary
  for complete Table 3 comparison rows. Unresolved settings are explicit nulls;
  the repository does not fill missing measurements with paper cells or estimates.
- Data downloads require network access and adequate storage. Conversion and
  query evaluation recompute ground truth on real input and can be expensive.

The small, actual diagnostic raw CSVs and run manifests are retained under
`validation/raw/`; their plots can be regenerated without the vector dataset.
Full preprocessing outputs, build logs and figures remain local. New run output
directories are ignored by Git. No vectors, baseline code, result workbooks or
original paper measurements are included in the source package.
