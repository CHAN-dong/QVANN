"""Run the unmodified author's Python/Rust implementation in isolated processes."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "upstream"
sys.path.insert(0, str(UPSTREAM))


def metrics(values):
    return dict(zip(("build_seconds", "prove_seconds", "verify_seconds",
                     "compressed_proof_bytes", "prove_sampled_peak_rss_bytes",
                     "gates_before_build"), values))


def synthetic(config, system):
    from bench.circuit_based import bench as circuit
    from bench.set_based import bench as multiset
    configs = {
        "smoke": (256, 16, 2, 4, 8, 2, 4),
        "basic": (8192, 128, 8, 16, 256, 16, 64),
    }
    N, D, M, K, lists, probes, k = configs[config]
    values = (multiset if system == "multiset" else circuit)(
        D, lists, M, K, D // M, probes, N // lists, k, True)
    return {"kind": "author_synthetic_proof_cost", "system": system,
            "config": dict(N=N, D=D, M=M, K=K, n_list=lists, n_probe=probes,
                           top_k=k, merkled=True), "metrics": metrics(values),
            "proof_generation_and_internal_verification": "passed"}


def sift():
    import faiss
    import numpy as np
    from ivf_pq.zk import ivf_pq_learn
    from ivf_pq.merkle_zk import zk_ivf_pq_query
    faiss.omp_set_num_threads(1)
    data = ROOT.parent / "dataset" / "sift"

    def read(path, count):
        with path.open("rb") as f:
            dim = int(np.fromfile(f, dtype="<i4", count=1)[0])
        a = np.fromfile(path, dtype="<i4", count=count * (dim + 1)).reshape(-1, dim + 1)
        assert np.all(a[:, 0] == dim)
        return a[:, 1:].copy().view("<f4").astype(np.int64)

    base = read(data / "sift_base.fvecs", 4096)
    queries = read(data / "sift_query.fvecs", 10)
    start = time.perf_counter()
    labels, centers, books, codes, groups, moved = ivf_pq_learn(
        base, n_list=32, n_iter=10, M=8, K=16, random_state=0, cluster_bound=128)
    train_seconds = time.perf_counter() - start
    recalls, returned = [], []
    for query in queries:
        ids, _ = zk_ivf_pq_query(query, centers, books, codes, groups,
                               top_k=10, n_probe=4, proof=False)
        exact = np.argsort(np.sum((base - query) ** 2, axis=1), kind="stable")[:10]
        recalls.append(len(set(ids.tolist()) & set(exact.tolist())) / 10)
        returned.append(ids)
    proved_ids, values = zk_ivf_pq_query(queries[0], centers, books, codes, groups,
                                       top_k=10, n_probe=4, proof=True)
    assert np.array_equal(proved_ids, returned[0]), "proof/fast-path top-k mismatch"
    return {"kind": "real_sift_subset", "N": len(base), "D": base.shape[1],
            "queries": len(queries), "n_list": 32, "n_probe": 4, "M": 8, "K": 16,
            "top_k": 10, "rebalance_bound": 128,
            "actual_padded_capacity": 1 << (max(map(len, groups.values())) - 1).bit_length(),
            "training_seconds": train_seconds, "relocated_vectors": int(moved),
            "recall_at_10_set_overlap": float(np.mean(recalls)),
            "per_query_recall": recalls, "first_query_ids": proved_ids.tolist(),
            "first_query_metrics": metrics(values),
            "proof_generation_and_internal_verification": "passed",
            "proof_fast_path_topk_match": True,
            "note": "4096-vector subset ground truth recomputed locally; not paper Table 6."}


def tamper():
    from bench import data_gen
    import numpy as np
    from zk_IVF_PQ.zk_IVF_PQ import py_set_based_with_merkle
    q, centers, selected, codes, valid, items, books, roots = data_gen(16, 8, 2, 4, 8, 2, 32)
    dist = np.sum((centers - q) ** 2, axis=1).astype(np.int64)
    order = np.argsort(dist, kind="stable")
    pairs = np.column_stack((order, dist[order]))
    # Commitment is built from the original roots; the opened PQ record is changed.
    codes[0, 0, 0] = (codes[0, 0, 0] + 1) % 4
    try:
        py_set_based_with_merkle(q, centers, codes, valid, items, books, roots, 4, pairs, [])
    except BaseException as exc:
        # Plonky2 may expose an inconsistent witness as PyO3 PanicException.
        if not isinstance(exc, RuntimeError) and type(exc).__name__ != "PanicException":
            raise
        return {"kind": "negative_test", "alteration": "opened PQ code changed, original roots retained",
                "rejected": True, "error_type": type(exc).__name__, "error": str(exc)}
    raise AssertionError("Tampered opened record was accepted")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--worker", choices=["smoke", "basic", "sift", "tamper"])
    p.add_argument("--system", choices=["multiset", "circuit"], default="multiset")
    p.add_argument("--suite", choices=["smoke", "basic", "all"], default="all")
    args = p.parse_args()
    (ROOT / "results").mkdir(exist_ok=True)
    (ROOT / "logs").mkdir(exist_ok=True)
    if args.worker:
        if args.worker == "sift":
            result = sift()
        elif args.worker == "tamper":
            result = tamper()
        else:
            result = synthetic(args.worker, args.system)
        result.update(python=sys.version, platform=platform.platform(),
                      rayon_threads=os.environ.get("RAYON_NUM_THREADS"),
                      upstream_commit=subprocess.check_output(
                          ["git", "rev-parse", "HEAD"], cwd=UPSTREAM, text=True).strip())
        path = ROOT / "results" / f"{args.worker}-{args.system}.json"
        path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result, indent=2), flush=True)
        return
    jobs = [("smoke", "multiset"), ("smoke", "circuit"), ("tamper", "multiset")]
    if args.suite in ("basic", "all"):
        jobs += [("basic", "multiset"), ("basic", "circuit")]
    if args.suite == "all":
        jobs += [("sift", "multiset")]
    env = dict(os.environ, RAYON_NUM_THREADS="4", OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")
    outcomes = []
    for job, system in jobs:
        name = f"{job}-{system}"
        print(f"Running {name}", flush=True)
        # Remove stale success evidence before each new attempt.
        (ROOT / "results" / f"{name}.json").unlink(missing_ok=True)
        with (ROOT / "logs" / f"{name}.log").open("w", encoding="utf-8") as log:
            proc = subprocess.run([sys.executable, __file__, "--worker", job,
                                   "--system", system], cwd=UPSTREAM, env=env,
                                  stdout=log, stderr=subprocess.STDOUT)
        outcomes.append({"job": name, "exit_code": proc.returncode})
        print(outcomes[-1], flush=True)
    (ROOT / "results" / "suite.json").write_text(json.dumps(outcomes, indent=2) + "\n")
    if any(x["exit_code"] for x in outcomes):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
