#!/usr/bin/env python3
"""Build a baseline command, run it and export query measurements."""

import argparse
import csv
import json
import math
import os
from pathlib import Path
import subprocess
import time

from reproduce import FIELDS as COMMON_FIELDS, sha

ROOT = Path(__file__).resolve().parents[1]
FIELDS = COMMON_FIELDS + ["efSearch", "probes", "leaf_capacity"]
METHODS = ("annproof", "vlah", "mr")


def write_csv(path, rows):
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def command_for(method, settings, binary, data, raw, cache):
    if method == "mr":
        if settings["query_offset"] != 100 or settings["fanout"] != 16:
            raise ValueError("MR-tree uses query offset 100 and fanout 16")
        return [str(binary), str(data / "sift_base.fvecs"), str(data / "sift_query.fvecs"),
                str(settings["N"]), str(raw), str(settings["k"]), str(settings["queries"]),
                str(settings["repeats"]), str(settings["leaf_capacity"])]
    command = [str(binary), "--dataset", str(data), "--base", str(settings["N"]),
               "--M", str(settings["M"]), "--ef-construction", str(settings["efConstruction"]),
               "--ef-search", str(settings["efSearch"]), "--k", str(settings["k"]),
               "--queries", str(settings["queries"]), "--query-offset", str(settings["query_offset"]),
               "--repeats", str(settings["repeats"]), "--build-threads", str(settings["build_threads"]),
               "--metric", "l2", "--proof", "multiproof", "--cache", str(cache), "--output", str(raw)]
    if method == "annproof":
        command += ["--shard-size", str(settings["shard_size"])]
    else:
        command += ["--bucket-size", str(settings["bucket_capacity"]), "--probes", str(settings["probes"]),
                    "--lsh-width", str(settings["lsh_width"]), "--seed", str(settings["lsh_seed"])]
    return command


def measurements(method, settings, raw, output, profile):
    source = raw / ("samples.csv" if method == "mr" else "per_query.csv")
    with source.open() as stream:
        native = list(csv.DictReader(stream))
    if len(native) != settings["queries"] * settings["repeats"]:
        raise ValueError("incomplete baseline measurements")
    digest = sha(source)
    rows = []
    for item in native:
        valid = item["valid"] if method == "mr" else item["verified"]
        if valid != "1":
            raise ValueError("baseline verification failed")
        rows.append(dict(
            experiment="table3", profile=profile, dataset=settings["dataset"],
            method=settings["label"], variant=settings["variant"], N=settings["N"],
            M="" if method == "mr" else settings["M"],
            efConstruction="" if method == "mr" else settings["efConstruction"],
            E="", k=item["k"], query=item["query_id"] if method == "mr" else item["query"],
            repeat=item["repeat"], recall=item["recall"] if method == "mr" else item["recall_at_k"],
            query_ms=item["query_ms"] if method == "mr" else item["server_total_ms"],
            verify_ms=item["verify_ms"] if method == "mr" else item["verification_ms"],
            vo_bytes=item["vo_bytes"], route_proof_bytes="" if method == "mr" else item["route_proof_bytes"],
            verified=valid, source_csv=str(source.relative_to(output)), source_sha256=digest,
            efSearch="" if method == "mr" else item["ef_search"],
            probes=item["probes"] if method == "vlah" else "",
            leaf_capacity=settings["leaf_capacity"] if method == "mr" else ""))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("method", choices=METHODS)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--preset", choices=["sift", "nytimes", "glove", "deep", "openai"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--config", type=Path, default=ROOT / "configs/baselines.json")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--profile", choices=["default", "paper", "diagnostic"], default="default")
    options = {"base": "N", "M": "M", "ef-construction": "efConstruction", "ef-search": "efSearch",
               "queries": "queries", "query-offset": "query_offset", "repeats": "repeats",
               "k": "k", "build-threads": "build_threads", "shard-size": "shard_size",
               "bucket-size": "bucket_capacity", "probes": "probes", "seed": "lsh_seed",
               "leaf-capacity": "leaf_capacity"}
    for flag in options:
        parser.add_argument("--" + flag, type=int)
    parser.add_argument("--lsh-width", type=float)
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    settings = {**config["common"], **config["datasets"][args.preset],
                **config["methods"][args.method], "dataset": args.preset}
    for flag, name in options.items():
        value = getattr(args, flag.replace("-", "_"))
        if value is not None:
            settings[name] = value
    if args.lsh_width is not None:
        settings["lsh_width"] = args.lsh_width
    if any(settings[name] < 1 for name in ["N", "queries", "repeats", "k", "build_threads"]):
        parser.error("base, queries, repeats, k and build threads must be positive")
    if settings["k"] > settings["N"] or settings["query_offset"] < 0:
        parser.error("invalid k or query offset")
    if args.method == "mr":
        if settings["leaf_capacity"] < 1 or settings["build_threads"] != 1:
            parser.error("MR-tree requires a positive leaf capacity and one thread")
    else:
        if settings["M"] < 2 or settings["efConstruction"] < 1 or settings["efSearch"] < 1:
            parser.error("invalid graph parameters")
        if settings["build_threads"] > 24:
            parser.error("construction threads must be between 1 and 24")
        if args.method == "annproof" and settings["shard_size"] < 1:
            parser.error("shard size must be positive")
        if args.method == "vlah" and (settings["bucket_capacity"] < 1 or settings["probes"] < 1
                or not math.isfinite(settings["lsh_width"]) or settings["lsh_width"] <= 0):
            parser.error("invalid bucket or LSH parameters")
    for name in ("sift_base.fvecs", "sift_query.fvecs"):
        if not (args.dataset / name).is_file():
            parser.error("missing input: " + str(args.dataset / name))
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("output must be new or empty")
    binary = args.build_dir.resolve() / args.method
    if not binary.is_file():
        parser.error("build the baseline executables with CMake first")
    output.mkdir(parents=True, exist_ok=True)
    raw = output / "raw"
    cache = (args.cache or ROOT / "cache" / args.preset / args.method).resolve()
    command = command_for(args.method, settings, binary, args.dataset.resolve(), raw, cache)
    environment = dict(os.environ)
    for name in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
        environment[name] = "1"
    record = {"status": "running", "method": args.method, "profile": args.profile, "settings": settings,
              "command": command, "binary_sha256": sha(binary), "config_sha256": sha(args.config),
              "inputs": {name: sha(args.dataset / name) for name in ("sift_base.fvecs", "sift_query.fvecs")}}
    manifest = output / "run.json"
    manifest.write_text(json.dumps(record, indent=2) + "\n")
    started = time.monotonic()
    try:
        with (output / "run.log").open("w") as log:
            subprocess.run(command, env=environment, check=True, stdout=log, stderr=subprocess.STDOUT)
        rows = measurements(args.method, settings, raw, output, args.profile)
        write_csv(output / "measurements.csv", rows)
        record["status"] = "passed"
    except Exception as error:
        record.update(status="failed", error=str(error))
        raise
    finally:
        record["wall_seconds"] = time.monotonic() - started
        manifest.write_text(json.dumps(record, indent=2) + "\n")
    print("Results:", output / "measurements.csv")


if __name__ == "__main__":
    main()
