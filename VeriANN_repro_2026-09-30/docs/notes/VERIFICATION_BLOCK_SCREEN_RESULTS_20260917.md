# Verification-aware block offline screen

The current SQ4 top-1 E=64 traces are fixed. Blocks are built globally using a rank
combination of SQ4 code distance, exact raw-vector radius, internal HNSW links, and
level-0 neighbor overlap. Exact raw vectors define the quantized-center covering radius.

| size | candidate blocks | candidate objects | inflation | excluded blocks | opened objects | recall | screen payload |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 | 684.52 | 1369.04 | 1.69x | 16.39% | 1144.62 | 0.996 | 164.87 KiB |
| 4 | 574.44 | 2297.76 | 2.83x | 4.27% | 2199.64 | 0.995 | 223.72 KiB |

There are zero radius-bound violations. The comparable current object representation
cost is 70.25 KiB before the unchanged routing adjacency portion. Size-2 summaries
alone cost 72.20 KiB; size 4 saves summary bytes but opens too many co-members. Since
the screen already costs 2.35x/3.18x more before adding a block graph and its adjacency,
the full degree-16/24/32 block-graph implementation is stopped.
