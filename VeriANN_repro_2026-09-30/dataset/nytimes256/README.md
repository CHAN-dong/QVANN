# nytimes256

Base: 289,761 vectors; dimension: 256; queries: 10,000.

Metric: cosine via unit-normalized squared L2. Existing benchmark argument: `--metric l2`.

Recompute exact top-k on prepared normalized float32 base for each N and queries100..119; supplied source neighbors retained as reference only.

sift_base/query.fvecs are hard links for existing loader compatibility; the contents are this named dataset, not SIFT.

Original downloads are retained in `raw/`. Source URLs, verified HTTP ranges, ETags, SHA-256 fingerprints, preprocessing and converted shapes are in `provenance.json`.

Dataset preparation does not establish an experiment result. Build fresh indexes for this dataset and recalibrate all search parameters; do not reuse SIFT work points.
