# Chen13-MR: plaintext, high-dimensional exact kNN baseline

Source paper: Qian Chen, Haibo Hu, Jianliang Xu, *Authenticating Top-k Queries
in Location-based Services with Confidentiality*, PVLDB 7(1), 49–60 (2013),
https://www.vldb.org/pvldb/vol7/p49-chen.pdf. The author's publication page is
https://www.comp.hkbu.edu.hk/~qchen/ and the institutional record is
https://scholars.hkbu.edu.hk/en/publications/authenticating-top-k-queries-in-location-based-services-with-conf/.
This folder contains our implementation of the explicitly described plaintext
algorithm, using the supplied paper as its specification.

## Scope and deviations

- Implement **section 5.1.1 and Figure 6(a)**: best-first exact kNN on a Merkle
  R-tree, with answers, the unexpanded frontier's MBBs, necessary hashes and a
  signed root. The verifier checks authentication, answer ranking and completeness.
- Extend the MBB/minimum-distance formula to the original full dimension, with
  nonspatial scores set to zero. No projection, PQ, approximate search, privacy,
  Paillier, PPB, PLB, Power Diagram or private ranking comparisons.
- This is **exact kNN**, not an approximate nearest-neighbor algorithm. Exact
  distances use double accumulation on the existing float32 coordinates; equal
  distances are ordered by original zero-based ID. Only strictly larger MBB
  bounds permit pruning. No rounded/quantized bounds and no epsilon pruning.
- **Index construction differs from the paper's R*-tree.** We use a deterministic
  packed R-tree: fanout 16, leaf capacity 64, recursively partition on the largest
  sampled coordinate range, then compute exact bounds. The sampled range affects
  packing only; it never affects correctness. This is a high-dimensional
  adaptation of the plaintext MR-tree, not an author-code reproduction of the
  privacy-preserving MR/EMR/PD curves. It must be named accordingly in a paper.
  These fixed packing parameters are not an exhaustive R-tree tuning study;
  measured costs cannot be attributed to every possible high-dimensional
  R*-tree/EMR implementation or to the paper's low-dimensional experiments.
- SHA-256 commitments have explicit node/content domains; leaf records directly
  commit IDs together with coordinates instead of a redundant nested ID digest.
  Bounds are bound to content hashes. Ed25519 signs the dataset/configuration/root
  commitment. The trusted client anchor contains that root, metadata and public
  key; it is provisioned offline, like the other authenticated baselines.
- Opened MBBs are recomputed from children rather than transmitted redundantly.
  A closed node carries its full-dimensional MBB and content digest. Every tree
  branch appears exactly once. An opened leaf carries every point in that leaf.
  There is no compression, hidden answer-only proof, query-result cache, or
  replacement of the completeness check by an ID membership check.

## Protocol

- Four active datasets: SIFT1M, NYTimes-256, GloVe-100, DEEP1M. Existing normalized
  NYTimes/GloVe files are used unchanged, so L2 ranking follows the existing setup.
- Query IDs 100–119, 3 repetitions: 60 real search/proof/verify samples per point.
  Search, build and verification use one native thread. Builds run once from
  scratch. All output is measured, not forecast.
- Default k=10 on all four. k={1,5,10,20,50,100} **only SIFT and DEEP**.
  N={100K,500K,1M,5M,10M} **only SIFT and DEEP**. The SIFT scale panel uses
  BIGANN prefixes, as the existing scale table does; it is not the separate
  standard SIFT1M default. DEEP uses same-family prefixes and recomputes ground
  truth for each prefix. A complete independent exhaustive scan establishes
  ground truth for all 20 queries before timing each group.
- Query time = best-first search + generating/serializing the actual wire VO.
  Verification = decode, signature, Merkle recomputation, ranking and completeness.
  Neither includes network/disk transfer. VO includes its root, signature, query
  commitment and answer IDs. KB=1000 bytes; MB=1,000,000 bytes.
- Index size is the actual complete uncompressed serving snapshot, with spatially
  ordered original vectors/IDs, node bounds, topology, authentication digests,
  metadata, signature and public key. No duplicate vector copy or query VO is
  counted. Build time excludes source read/validation, exhaustive ground truth
  and final snapshot I/O. Snapshot I/O is reported separately. Peak RSS covers
  the whole process, including the proof/tamper tests, and is not index size.
- Each k runs nine negative tests on a real serialized VO: corrupt signature,
  query commitment, answer ID, middle/tail payload, truncate, append, change the
  expected query, and change expected k. Wrong answers, duplicate opened IDs,
  malformed trees, duplicate opened IDs, trailing bytes and incomplete frontier bounds
  are rejected by the verifier. `Verifier` receives no base/index reference.
- The recall panel has **one 100% exact reference point**, not fabricated lower
  recall operating points. Existing approximate methods remain at their previous
  defaults (about 95%), and V3DB retains its separate low-recall default. The
  ablation panel includes this as an external reference, not an Ours ablation.

## Run

From the inner project root under WSL:

```sh
g++ -O3 -DNDEBUG -std=c++17 Chen13-MR/mr.cpp -lcrypto -o Chen13-MR/mr
/root/.venvs/v3db-work9/bin/python Chen13-MR/run.py smoke
/root/.venvs/v3db-work9/bin/python Chen13-MR/run.py run
/root/.venvs/v3db-work9/bin/python Chen13-MR/compatibility.py
/root/.venvs/v3db-work9/bin/python Chen13-MR/fill.py
/root/.venvs/v3db-work9/bin/python Chen13-MR/audit.py
/root/.venvs/v3db-work9/bin/python Chen13-MR/report.py
```

Requires C++17, OpenSSL, Python/openpyxl/numpy. No ANN library is needed.
The compatibility check compares every evaluated query/k with the existing
project's scalar float32 L2 + ID reference; all measured top-k sets agree.
Runs are checkpointed by immutable `measurement.json` records. Raw samples,
native metadata, signed proof examples and serving snapshots are under
`results/work9-chen13-20261021/`; protocol, backup and cell mappings are under
`experiments/work9-chen13-20261021/`. A 50 GiB address-space guard is imposed by
the runner; failures are recorded rather than filled as measurements.

## Binary encodings (little-endian)

Snapshot: seven uint32 fields (magic, N,D,fanout,leafcap,nodecount,rootindex), then
N packed records (uint32 ID + D float32 coordinates), then nodes in preorder:
begin/end/childcount uint32, child indices, lower/upper bounds (D floats each),
content SHA-256, node SHA-256. Finally 64-byte signature and 32-byte public key.
The private signing key is never saved.

VO: four uint32 fields (magic,N,D,k), root32, signature64, query hash32, k IDs,
then the root node recursively. Node tags: 0=MBB+content hash; 1=uint32 count+
ID/vector records; 2=uint32 count+recursive children. Leaf content hashes the
uint32 leaf domain/count+records. Internal content hashes the uint32 internal
domain/count+child node hashes. Node hash commits uint32 node domain+bounds+
content hash. The signed anchor commits version,N,D,fanout,leafcap+root node hash.
