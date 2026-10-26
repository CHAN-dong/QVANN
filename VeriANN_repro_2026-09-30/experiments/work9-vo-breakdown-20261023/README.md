# VO byte breakdown ? 2026-10-23

Five methods (MR-tree, ANNProof, VLAH, IVF-ADS, HNSW-ADS) across the existing four full-scale datasets, k=10, query IDs100..119, three repeats. All byte totals are actual serialized VO lengths. No performance prediction is used.

T contains vector/ID/adjacency records, MBB pruning witnesses and general control framing. A contains the query commitment, separately encoded authentication proofs with their proof-specific encoding, and VLAH's complete authenticated routing proof. In the plaintext MR-tree, the trusted root/signature transmitted in the VO and unopened frontier content hashes count as A, while MBBs and node tags/counts count as T. This declared structural partition is not the only possible semantic partition; use it consistently. For every sample T+A equals the full VO. Result-only is k*(4+4*d) bytes, with no authentication/header overhead; result records are already part of T and are not subtracted twice.

IVF-ADS is ablation variant1 (Merkle+IVF) and HNSW-ADS is variant2 (Merkle+HNSW). They do not use the progressive quantization or decoupled traversal optimizations. Existing GloVe ablations used only a 1M prefix, so this task trains/calibrates a matching IVF index on the full1183514 vectors and freshly measures variants1/2 at >=95% search-calibrated recall. Other existing baseline defaults are preserved. MR-tree has100% exact recall; other methods have their own >=95% configurations rather than identical realized recall.

MR-tree VOs are freshly generated/verified for all four datasets, and their full lengths must match every original measured sample. Other complete, verified20x3 raw sample groups are reused where available. SIFT's former overall tables used100 queries; this uniform20-query breakdown can therefore have different means from those overall rows.

The earlier requested ANNProof VO factor2 is represented separately: raw measurements are preserved; adjusted T and A are each multiplied by2 and labeled ANNProof*. This does not represent a physically enlarged serialized proof. Both raw and adjusted LaTeX tables are exported. For a paper reporting real measurements, use the raw version or explicitly disclose any adjusted presentation.

Run and fill via the project's WSL Python:

```
python scripts/run_work9_vo_breakdown_20261023.py
python scripts/fill_work9_vo_breakdown_20261023.py
```

The independent fill audit parses all20 retained query0 VOs byte by byte, cross-checks their recorded payload/local-proof/routing-proof lengths, checks1200 additive byte accounts and all scaling, and preserves every pre-existing workbook value/comment/style. If work9.xlsx is locked, the audited result is saved as work9_vo_breakdown_updated.xlsx; apply only after checking that the target workbook has no unsaved changes and still matches the backup.
