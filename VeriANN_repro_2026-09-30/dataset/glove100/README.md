# glove100

Base: 1,183,514 vectors; dimension: 100; queries: 10,000.

Metric: cosine via unit-normalized squared L2. Existing benchmark argument: `--metric l2`.

Provided top-100 IDs for the full source base. Recompute if using a smaller base subset; angular ranking is represented through normalization.

sift_base/query.fvecs are hard links for existing loader compatibility; the contents are this named dataset, not SIFT.

Original downloads are retained in `raw/`. Source URLs, verified HTTP ranges, ETags, SHA-256 fingerprints, preprocessing and converted shapes are in `provenance.json`.

Dataset preparation does not establish an experiment result. Build fresh indexes for this dataset and recalibrate all search parameters; do not reuse SIFT work points.
