# deep10m

Base: 10,000,000 vectors; dimension: 96; queries: 10,000.

Metric: squared L2. Existing benchmark argument: `--metric l2`.

Computed exactly by each benchmark for N=5M/10M and the same selected query IDs. Never reuse 1M neighbors.

sift_base/query.fvecs are hard links for existing loader compatibility; the contents are this named dataset, not SIFT.

Original downloads are retained in `raw/`. Source URLs, verified HTTP ranges, ETags, SHA-256 fingerprints, preprocessing and converted shapes are in `provenance.json`.

Dataset preparation does not establish an experiment result. Build fresh indexes for this dataset and recalibrate all search parameters; do not reuse SIFT work points.
