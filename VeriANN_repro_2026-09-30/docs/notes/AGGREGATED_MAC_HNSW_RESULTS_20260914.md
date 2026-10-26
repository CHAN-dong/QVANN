# Aggregated MAC authentication for Relaxed HNSW + SQ6

The search trace is unchanged: Recall@10 0.991, 62.99 expanded nodes, 833.23
disclosed SQ6 records, 67.74 opened adjacency records, and 22.74 raw records.

Each SQ6, raw, and complete `(node, level)` adjacency-list record has an independently
precomputed 128-bit HMAC-SHA256 tag. A client-supplied random 128-bit nonce derives
per-query coefficients from `(nonce, domain, id, level)`. Tags are combined by a
weighted sum in GF(2^128), producing one 16-byte aggregate per record domain.

The response authentication is therefore 48 bytes. The nonce is sent with the query
and is not response VO. Payload is unchanged at 98.643 KiB, giving 98.690 KiB total.
This reduces authentication by 99.973% from 170.477 KiB and total VO by 63.329% from
269.12 KiB.

Pre-generated tags: 100,000 SQ6, 106,766 adjacency, and 100,000 raw. Total storage is
4.681 MiB, or 49.083 bytes/object. A single SQ6/raw/adjacency-list update regenerates
one corresponding 16-byte tag; reciprocal graph edits regenerate one tag per changed
adjacency list.

Final run timings: SQ6 pre-generation 166.99 ms, adjacency 176.77 ms, raw 173.08 ms,
total 516.84 ms. Server aggregation is 1.847 ms/query and client recomputation and
verification is 3.423 ms/query. Three runs ranged from 1.784--1.917 ms server-side
and 3.335--3.585 ms client-side. All 100 aggregate verifications passed.

Security scope: this is symmetric verification. The client must replay the deterministic
traversal to determine the required record IDs; otherwise a malicious server could omit
a complete record and aggregate only the subset it chose to return. Public verifiability
still requires a signature/vector-commitment-style construction rather than this MAC.
