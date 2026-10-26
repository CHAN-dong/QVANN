# deep1m

Base: 1,000,000 vectors; dimension: 96; queries: 10,000.

Metric: squared L2. Existing benchmark argument: `--metric l2`.

Not supplied for the 1M prefix. Existing benchmark computes exact ground truth for the requested queries and base size. Do not use 1B/10M ground truth for this 1M subset.

sift_base/query.fvecs are hard links for existing loader compatibility; the contents are this named dataset, not SIFT.

Original downloads are retained in `raw/`. Source URLs, verified HTTP ranges, ETags, SHA-256 fingerprints, preprocessing and converted shapes are in `provenance.json`.

Dataset preparation does not establish an experiment result. Build fresh indexes for this dataset and recalibrate all search parameters; do not reuse SIFT work points.

To extend to N rows, fetch bytes 0..(8+N*96*4-1) from the same base.1B.fbin; preserve query IDs and recompute exact neighbors for N.
