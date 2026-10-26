# Pure index-design VO experiments

Dataset: first 100,000 SIFT base vectors, 50 queries, exact top-100 ground truth recomputed on this subset. Payload model uses a 72-byte authenticated SQ4 record per visited object, plus 512 bytes when its certified lower bound cannot exclude it. Static centroids are assumed cached; 32 bytes per probed-list routing hash are included.

## What did not work well

- Fixed similarity/ball blocks: useful mainly when almost all IVF lists are probed.
- Query-aware block sizing: small improvement, insufficient to justify the complexity.
- Multi-pivot interval blocks: negative VO change for nprobe <= 64; best case about 10.5% only at nprobe=100.
- Quantized full-dimensional boxes: best batch bound so far, but only 2.1% at nprobe=64 and negative at nprobe <= 32; 27.1% at nprobe=100.

The shared failure mode is overlap: once a block contains several 128-D points, its safe lower bound becomes loose. Summary overhead is paid before it can reject the block.

## Hierarchical fine IVF

Finer, deterministically trained hierarchical IVF reduces the number of objects that need certificates. Selected results:

| nlist | nprobe | recall@100 | visited | VO/query |
|---:|---:|---:|---:|---:|
| 128 | 16 | 0.9634 | 13,149 | 1.256 MB |
| 1024 | 64 | 0.9596 | 7,090 | 0.808 MB |
| 128 | 32 | 0.9986 | 25,973 | 2.195 MB |
| 1024 | 128 | 0.9920 | 13,917 | 1.324 MB |

At roughly 0.96 recall, fine IVF lowers VO by 35.7%; around 0.99 recall it lowers VO by about 39.7% (with the latter comparison giving the 128-list configuration slightly higher recall). This is materially better than adding summaries inside large posting lists, but is not an order-of-magnitude reduction.

## Most promising next structure

Use a two-stage authenticated index: fine IVF routing followed by compact geometric leaves (small quantized boxes or balls). The coarse routing must eliminate most objects; leaf summaries should only target the remaining boundary lists. Avoid large blocks and avoid proving per-object search control flow.

The fundamental limitation remains: without a circuit, a block can be omitted only when a public authenticated bound proves every member is outside the result radius. High-dimensional overlap limits how many objects one summary can safely eliminate.
