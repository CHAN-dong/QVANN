#!/usr/bin/env python3
"""Run QVANN with a dataset preset or explicit graph and query parameters."""

import argparse
import os
import subprocess
import time
from pathlib import Path

PRESETS = {
    "sift": (1000000, 8, 100, 128),
    "nytimes": (289761, 16, 200, 256),
    "glove": (1183514, 16, 200, 1536),
    "deep": (1000000, 16, 200, 64),
}


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--preset", choices=PRESETS)
    parser.add_argument("--base", type=int)
    parser.add_argument("--M", type=int)
    parser.add_argument("--ef-construction", type=int)
    parser.add_argument("--budget", type=int)
    parser.add_argument("--k", type=int, default=10)
    parser.add_argument("--queries", type=int, default=20)
    parser.add_argument("--query-offset", type=int, default=100)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--cache", type=Path, default=root / "cache")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--build-dir", type=Path, default=root / "build")
    args = parser.parse_args()
    defaults = PRESETS.get(args.preset, (100000, 16, 100, 64))
    values = (args.base, args.M, args.ef_construction, args.budget)
    base, degree, construction, budget = [
        default if value is None else value for value, default in zip(values, defaults)
    ]
    binary = args.build_dir.resolve() / "qvann"
    if not binary.is_file():
        parser.error("build QVANN with CMake first")
    for name in ("sift_base.fvecs", "sift_query.fvecs"):
        if not (args.dataset / name).is_file():
            parser.error("missing input: " + str(args.dataset / name))
    output = (args.output or root / "results" / time.strftime("%Y%m%d-%H%M%S")).resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("choose a new or empty output directory")
    environment = dict(os.environ)
    for name in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
        environment[name] = "1"
    command = [
        str(binary), "--dataset", str(args.dataset.resolve()), "--base", str(base),
        "--M", str(degree), "--ef-construction", str(construction), "--budget", str(budget),
        "--k", str(args.k), "--queries", str(args.queries),
        "--query-offset", str(args.query_offset), "--repeats", str(args.repeats),
        "--build-threads", "1", "--cache", str(args.cache.resolve()), "--output", str(output),
    ]
    subprocess.run(command, env=environment, check=True)
    print("Results:", output)


if __name__ == "__main__":
    main()
