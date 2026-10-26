import csv
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
rows = []
for name in ("smoke-multiset", "smoke-circuit", "basic-multiset", "basic-circuit", "sift-multiset"):
    result = json.loads((ROOT / "results" / f"{name}.json").read_text())
    m = result.get("metrics", result.get("first_query_metrics"))
    rows.append(dict(job=name, build_s=m["build_seconds"], prove_s=m["prove_seconds"],
                     verify_ms=m["verify_seconds"] * 1000,
                     compressed_proof_bytes=m["compressed_proof_bytes"],
                     prove_sampled_peak_rss_GiB=m["prove_sampled_peak_rss_bytes"] / 2**30,
                     gates_before_build=m["gates_before_build"]))
with (ROOT / "results" / "summary.csv").open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
assert rows[2]["gates_before_build"] == 21198
assert rows[3]["gates_before_build"] == 101016
assert json.loads((ROOT / "results" / "tamper-multiset.json").read_text())["rejected"]
assert all(x["exit_code"] == 0 for x in json.loads((ROOT / "results" / "suite.json").read_text()))
sources = {}
for rel in ("dataset/sift/sift_base.fvecs", "dataset/sift/sift_query.fvecs"):
    path = ROOT.parent / rel
    sources[rel] = {"bytes": path.stat().st_size,
                    "sha256": hashlib.file_digest(path.open("rb"), "sha256").hexdigest()}
upstream_status = subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT / "upstream", text=True)
assert not upstream_status, upstream_status
(ROOT / "results" / "validation.json").write_text(json.dumps({
    "all_jobs_passed": True, "basic_gate_counts_match_paper_table_8": True,
    "upstream_tracked_files_unchanged": True, "datasets": sources,
    "basic_prove_speedup": rows[3]["prove_s"] / rows[2]["prove_s"],
}, indent=2) + "\n")
print(json.dumps(rows, indent=2))
