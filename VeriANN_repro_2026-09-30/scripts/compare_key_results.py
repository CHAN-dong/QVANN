#!/usr/bin/env python3
import csv
import math
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = ROOT / "results" / "key"
ACTUAL = ROOT / "results" / "reproduced"
FILES = [
    "e_budget_relaxed_hnsw_q100.csv",
    "expansion_centric_merkle_q100.csv",
    "refinement_merkle_ordering_q100.csv",
]
IGNORED_COLUMNS = {
    "routing_ms",
    "exact_topk_ms",
    "vo_generation_ms",
    "total_query_compute_ms",
}
ABS_TOLERANCE = 1e-4


def load(path: Path):
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        return reader.fieldnames, list(reader)


def compare(name: str) -> bool:
    expected_header, expected_rows = load(EXPECTED / name)
    actual_header, actual_rows = load(ACTUAL / name)
    if expected_header != actual_header or len(expected_rows) != len(actual_rows):
        print(f"FAIL {name}: CSV shape/header mismatch")
        return False
    maximum_error = 0.0
    for row_number, (expected, actual) in enumerate(zip(expected_rows, actual_rows), start=2):
        for column in expected_header:
            if column in IGNORED_COLUMNS:
                continue
            try:
                left = float(expected[column])
                right = float(actual[column])
            except ValueError:
                if expected[column] != actual[column]:
                    print(f"FAIL {name}:{row_number}:{column}: {actual[column]} != {expected[column]}")
                    return False
                continue
            error = abs(left - right)
            maximum_error = max(maximum_error, error)
            if not math.isclose(left, right, rel_tol=0.0, abs_tol=ABS_TOLERANCE):
                print(f"FAIL {name}:{row_number}:{column}: {right} != {left}")
                return False
    print(f"PASS {name}: max numeric error={maximum_error:g}")
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Compare a fresh run with packaged references")
    parser.add_argument("--actual", type=Path, default=ACTUAL)
    ACTUAL = parser.parse_args().actual.resolve()
    missing = [name for name in FILES if not (ACTUAL / name).exists()]
    if missing:
        raise SystemExit("Missing reproduced files: " + ", ".join(missing))
    checks = [compare(name) for name in FILES]
    raise SystemExit(0 if all(checks) else 1)

