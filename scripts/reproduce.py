#!/usr/bin/env python3
"""Run measured QVANN Table 4 rows or the Figure 16 authentication ablation."""

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
FIELDS = ["experiment", "profile", "dataset", "method", "variant", "N", "M", "efConstruction",
          "E", "k", "query", "repeat", "recall", "query_ms", "verify_ms", "vo_bytes",
          "route_proof_bytes", "verified", "source_csv", "source_sha256"]


def sha(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def write_csv(path, rows):
    if not rows:
        raise ValueError("no measurements to export")
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("experiment", choices=["table4", "figure16"])
    parser.add_argument("--data-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--config", type=Path, default=ROOT / "configs/paper.json")
    parser.add_argument("--datasets", help="comma-separated table4 datasets")
    parser.add_argument("--cache", type=Path, default=ROOT / "cache")
    parser.add_argument("--require-historical-index", action="store_true")
    parser.add_argument("--diagnostic-base", type=int, help="explicit non-paper subset")
    parser.add_argument("--diagnostic-queries", type=int, default=2)
    parser.add_argument("--diagnostic-repeats", type=int, default=1)
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    if args.require_historical_index and args.diagnostic_base:
        parser.error("historical indexes cannot be used for diagnostic subsets")
    if args.require_historical_index and args.experiment != "table4":
        parser.error("historical graph fingerprints are recorded only for Table 4")
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("output must be new or empty; previous runs are never overwritten")
    if any(v < 1 for v in [args.diagnostic_queries, args.diagnostic_repeats]) or (
        args.diagnostic_base is not None and args.diagnostic_base < 10
    ):
        parser.error("invalid diagnostic dimensions")
    diagnostic = args.diagnostic_base is not None
    profile = "diagnostic" if diagnostic else "paper"
    binary = args.build_dir.resolve() / ("qvann" if args.experiment == "table4" else "granularity")
    if not binary.is_file():
        parser.error("missing executable: " + str(binary))
    if args.experiment == "table4":
        selected = args.datasets.split(",") if args.datasets else list(config["table4"])
        if any(name not in config["table4"] for name in selected):
            parser.error("unknown dataset")
        cases = [(name, config["table4"][name].copy()) for name in selected]
    else:
        if args.datasets and args.datasets != "sift":
            parser.error("Figure 16 in revision (8) uses SIFT")
        cases = []
        for degree in config["figure16"]["M"]:
            settings = config["figure16"].copy()
            settings["M"] = degree
            cases.append(("sift", settings))
    args.output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    # No inherited index override may silently change the selected graph.
    environment.pop("QVANN_INDEX_PATH", None)
    for name in ["OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"]:
        environment[name] = "1"
    try:
        revision = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        revision = "unavailable (source archive)"
    run = {"experiment": args.experiment, "profile": profile, "status": "running",
           "configuration": config, "config_sha256": sha(args.config), "revision": revision,
           "binary_sha256": sha(binary), "platform": platform.platform(),
           "python": sys.version, "cases": []}
    for name, command in [("compiler", ["c++", "--version"]), ("cmake", ["cmake", "--version"])]:
        run[name] = subprocess.check_output(command, text=True)
    run["source_dirty"] = bool(subprocess.run(
        ["git", "-C", str(ROOT), "diff", "--quiet", "HEAD"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode)
    manifest = args.output / "run.json"
    manifest.write_text(json.dumps(run, indent=2) + "\n")
    measured = []
    try:
        for dataset, settings in cases:
            if diagnostic:
                settings.update(N=args.diagnostic_base, queries=args.diagnostic_queries,
                                repeats=args.diagnostic_repeats, build_threads=1)
            data = args.data_root.resolve() / dataset
            for name in ["sift_base.fvecs", "sift_query.fvecs"]:
                if not (data / name).is_file():
                    raise FileNotFoundError(data / name)
            case = {"dataset": dataset, "settings": settings, "inputs": {
                name: sha(data / name) for name in ["sift_base.fvecs", "sift_query.fvecs"]}}
            if args.experiment == "table4" and not diagnostic and settings.get("query_file_sha256"):
                if case["inputs"]["sift_query.fvecs"] != settings["query_file_sha256"]:
                    raise ValueError("query file differs from the historical input")
            cache = args.cache.resolve() / dataset
            if args.require_historical_index:
                if not settings.get("historical_graph_sha256"):
                    raise ValueError("no archived graph fingerprint for " + dataset)
                expected = settings["historical_graph_sha256"]
                graph = cache / (settings["base_payload_sha256"][:16] +
                                 f"_M{settings['M']}_efc{settings['efConstruction']}.index")
                if not graph.is_file() or sha(graph) != expected:
                    raise ValueError(f"missing or mismatched historical graph: {graph}")
            tag = dataset if args.experiment == "table4" else f"{dataset}-M{settings['M']}"
            output = args.output.resolve() / tag
            command = [str(binary), "--dataset", str(data), "--base", str(settings["N"]),
                       "--M", str(settings["M"]), "--ef-construction", str(settings["efConstruction"]),
                       "--queries", str(settings["queries"]), "--query-offset", str(settings["query_offset"]),
                       "--repeats", str(settings["repeats"]), "--build-threads", str(settings["build_threads"]),
                       "--k", str(settings["k"]), "--cache", str(cache), "--output", str(output)]
            if args.experiment == "table4":
                command.extend(["--budget", str(settings["E"])])
            case["command"] = command
            run["cases"].append(case)
            started = time.monotonic()
            with (args.output / (tag + ".log")).open("w") as log:
                subprocess.run(command, env=environment, check=True, stdout=log, stderr=subprocess.STDOUT)
            case["wall_seconds"] = time.monotonic() - started
            source = output / ("per_query.csv" if args.experiment == "table4" else "samples.csv")
            with source.open() as stream:
                rows = list(csv.DictReader(stream))
            expected_count = settings["queries"] * settings["repeats"] * (1 if args.experiment == "table4" else 3)
            if len(rows) != expected_count:
                raise ValueError(f"incomplete measurements: {source}")
            digest = sha(source)
            if args.experiment == "table4" and settings.get("base_payload_sha256"):
                identity = json.loads((output / "manifest.json").read_text())
                case["payload_matches_historical"] = identity["dataset_sha256"] == settings["base_payload_sha256"]
                if not diagnostic and not case["payload_matches_historical"]:
                    raise ValueError("input payload differs from paper source; use the documented preprocessing")
            graphs = list(cache.glob(f"*_M{settings['M']}_efc{settings['efConstruction']}.index"))
            case["graph_files"] = {str(path): sha(path) for path in graphs}
            for row in rows:
                valid = row["verified"] if args.experiment == "table4" else row["valid"]
                if valid != "1":
                    raise ValueError("verification failure in raw measurements")
                native = args.experiment == "table4"
                result = dict(experiment=args.experiment, profile=profile, dataset=dataset,
                              method="QVANN", variant="ER" if native else row["variant"],
                              N=settings["N"], M=settings["M"], efConstruction=settings["efConstruction"],
                              E=row["budget"] if native else row["E"], k=settings["k"],
                              query=row["query"], repeat=row["repeat"],
                              recall=row["recall_at_k"] if native else row["recall"],
                              query_ms=float(row["search_ms"]) + float(row["vo_generation_ms"]) if native else row["query_ms"],
                              verify_ms=row["verification_ms"] if native else row["verify_ms"],
                              vo_bytes=row["vo_bytes"] if native else row["total_bytes"],
                              route_proof_bytes=row["route_proof_bytes"], verified=valid,
                              source_csv=str(source.relative_to(args.output.resolve())), source_sha256=digest)
                if not all(math.isfinite(float(result[field])) and float(result[field]) >= 0
                           for field in ["recall", "query_ms", "verify_ms", "vo_bytes", "route_proof_bytes"]):
                    raise ValueError("nonfinite or negative measurement")
                measured.append(result)
            write_csv(args.output / "measurements.csv", measured)
        run["status"] = "passed"
    except Exception as error:
        run.update(status="failed", error=str(error))
        raise
    finally:
        manifest.write_text(json.dumps(run, indent=2) + "\n")
    print("Actual measurements:", args.output / "measurements.csv")


if __name__ == "__main__":
    main()
