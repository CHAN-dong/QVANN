#!/usr/bin/env python3
"""Run integrity tests and a small QVANN query workload on temporary input."""

import argparse
import csv
import os
import random
import struct
import subprocess
import tempfile
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=root / "build")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    binary = build / "qvann"
    if not binary.is_file() or not (build / "test_tamper").is_file():
        parser.error("build QVANN with BUILD_TESTING=ON before running this check")

    environment = dict(os.environ)
    for name in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS"):
        environment[name] = "1"
    subprocess.run(
        ["ctest", "--test-dir", str(build), "--output-on-failure"],
        env=environment,
        check=True,
    )
    with tempfile.TemporaryDirectory(prefix="qvann-smoke-") as temporary:
        work = Path(temporary)
        data = work / "data"
        data.mkdir()
        rng = random.Random(20261023)
        dimension = 16
        for name, count in (("sift_base.fvecs", 2048), ("sift_query.fvecs", 120)):
            with (data / name).open("wb") as stream:
                for _ in range(count):
                    coordinates = [rng.gauss(0, 1) for _ in range(dimension)]
                    stream.write(struct.pack("<i" + "f" * dimension, dimension, *coordinates))
        output = work / "output"
        command = [
            str(binary), "--dataset", str(data), "--base", "2048",
            "--queries", "20", "--query-offset", "100", "--repeats", "3",
            "--M", "16", "--ef-construction", "100", "--budget", "32",
            "--cache", str(work / "cache"), "--output", str(output),
        ]
        subprocess.run(command, env=environment, check=True)
        with (output / "per_query.csv").open() as stream:
            rows = list(csv.DictReader(stream))
        if len(rows) != 60 or any(row["verified"] != "1" for row in rows):
            raise RuntimeError("honest-query verification failed")
        print("Smoke check passed: 60 verified query executions; integrity tests passed.")


if __name__ == "__main__":
    main()
