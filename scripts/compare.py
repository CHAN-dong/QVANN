#!/usr/bin/env python3
"""Run QVANN, ANNProof, VLAH and MR-tree on the same dataset/query windows."""

import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys

from baseline import FIELDS, METHODS, write_csv
from reproduce import sha

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--methods", default="qvann,annproof,vlah,mr")
    parser.add_argument("--datasets", default="sift,nytimes,glove,deep")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--cache", type=Path, default=ROOT / "cache")
    parser.add_argument("--config", type=Path, default=ROOT / "configs/paper.json")
    parser.add_argument("--baseline-config", type=Path, default=ROOT / "configs/baselines.json")
    parser.add_argument("--base", type=int, help="optional input prefix size")
    parser.add_argument("--queries", type=int, help="queries for a subset run (default: 2)")
    parser.add_argument("--repeats", type=int, help="repetitions for a subset run (default: 1)")
    args = parser.parse_args()
    methods = args.methods.split(",")
    datasets = args.datasets.split(",")
    if not methods or len(set(methods)) != len(methods) or any(m not in ("qvann", *METHODS) for m in methods):
        parser.error("choose distinct methods from qvann,annproof,vlah,mr")
    config = json.loads(args.config.read_text())
    if any(dataset not in config["table3"] for dataset in datasets) or len(set(datasets)) != len(datasets):
        parser.error("choose distinct datasets from sift,nytimes,glove,deep")
    if args.base is None and (args.queries is not None or args.repeats is not None):
        parser.error("--queries and --repeats require --base")
    args.queries = 2 if args.queries is None else args.queries
    args.repeats = 1 if args.repeats is None else args.repeats
    if args.base is not None and (args.base < 10 or args.queries < 1 or args.repeats < 1):
        parser.error("invalid subset parameters")
    for method in methods:
        if not (args.build_dir / method).is_file():
            parser.error("missing executable: " + str(args.build_dir / method))
    for dataset in datasets:
        for filename in ("sift_base.fvecs", "sift_query.fvecs"):
            if not (args.data_root / dataset / filename).is_file():
                parser.error("missing input: " + str(args.data_root / dataset / filename))
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("output must be new or empty")
    profile = "diagnostic" if args.base else "paper"
    output.mkdir(parents=True, exist_ok=True)
    run = {"status": "running", "experiment": "table3", "profile": profile, "methods": methods,
           "datasets": datasets, "configuration_sha256": sha(args.config),
           "baseline_configuration_sha256": sha(args.baseline_config), "commands": []}
    (output / "run.json").write_text(json.dumps(run, indent=2) + "\n")
    rows = []

    def execute(command, location):
        run["commands"].append(command)
        with (output / ("step-" + str(len(run["commands"])) + ".log")).open("w") as log:
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
        with (location / "measurements.csv").open() as stream:
            measured = list(csv.DictReader(stream))
        for item in measured:
            item["source_csv"] = str((location / item["source_csv"]).relative_to(output))
            rows.append({key: item.get(key, "") for key in FIELDS})
        write_csv(output / "measurements.csv", rows)

    try:
        if "qvann" in methods:
            location = output / "qvann"
            command = [sys.executable, str(ROOT / "scripts/reproduce.py"), "table3",
                       "--data-root", str(args.data_root.resolve()), "--datasets", ",".join(datasets),
                       "--config", str(args.config.resolve()), "--build-dir", str(args.build_dir.resolve()),
                       "--cache", str(args.cache.resolve()), "--output", str(location)]
            if args.base:
                command += ["--diagnostic-base", str(args.base), "--diagnostic-queries", str(args.queries),
                            "--diagnostic-repeats", str(args.repeats)]
            execute(command, location)
        for method in methods:
            if method == "qvann":
                continue
            for dataset in datasets:
                settings = config["table3"][dataset]
                location = output / method / dataset
                cache = args.cache.resolve() / dataset
                if method == "vlah":
                    cache = cache / "vlah"
                command = [sys.executable, str(ROOT / "scripts/baseline.py"), method,
                           "--dataset", str(args.data_root.resolve() / dataset), "--preset", dataset,
                           "--config", str(args.baseline_config.resolve()), "--output", str(location),
                           "--build-dir", str(args.build_dir.resolve()), "--cache", str(cache), "--profile", profile,
                           "--base", str(args.base or settings["N"]), "--k", str(settings["k"]),
                           "--queries", str(args.queries if args.base else settings["queries"]),
                           "--query-offset", str(settings["query_offset"]),
                           "--repeats", str(args.repeats if args.base else settings["repeats"])]
                execute(command, location)
        run["status"] = "passed"
    except Exception as error:
        run.update(status="failed", error=str(error))
        raise
    finally:
        (output / "run.json").write_text(json.dumps(run, indent=2) + "\n")
    print("Comparison results:", output / "measurements.csv")


if __name__ == "__main__":
    main()
