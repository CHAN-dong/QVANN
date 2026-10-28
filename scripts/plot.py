#!/usr/bin/env python3
"""Summarize CSV measurements and generate tables and figures."""

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("experiment", choices=["table4", "figure16"])
    parser.add_argument("--input", type=Path, required=True, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = []
    for source in args.input:
        run_file = source.parent / "run.json"
        if run_file.exists() and json.loads(run_file.read_text())["status"] != "passed":
            raise ValueError("cannot plot an incomplete or failed run")
        with source.open() as stream:
            records = list(csv.DictReader(stream))
        if not records:
            raise ValueError("empty measurements: " + str(source))
        checked = set()
        for row in records:
            evidence = (source.parent / row["source_csv"]).resolve()
            key = (evidence, row["source_sha256"])
            if key not in checked:
                actual = hashlib.sha256(evidence.read_bytes()).hexdigest()
                if actual != row["source_sha256"]:
                    raise ValueError("raw measurement checksum changed: " + str(evidence))
                checked.add(key)
        for record in records:
            if record["experiment"] == "table3":
                record["experiment"] = "table4"
        rows.extend(records)
    if len({row["profile"] for row in rows}) != 1:
        raise ValueError("paper and diagnostic runs cannot be combined")
    if any(row["experiment"] != args.experiment or row["verified"] != "1" for row in rows):
        raise ValueError("experiment mismatch or unverified measurements")
    profile = rows[0]["profile"]
    groups, identities = {}, set()
    for row in rows:
        key = tuple(row.get(name, "") for name in ["dataset", "method", "variant", "N", "M",
                                                 "efConstruction", "E", "efSearch", "probes", "leaf_capacity", "k"])
        identity = key + (row["query"], row["repeat"])
        if identity in identities:
            raise ValueError("duplicate query/repeat; do not merge a run twice")
        identities.add(identity)
        for name in ["recall", "query_ms", "verify_ms", "vo_bytes", "route_proof_bytes"]:
            if name == "route_proof_bytes" and row[name] == "":
                continue
            value = float(row[name])
            if not math.isfinite(value) or value < 0 or (name == "recall" and value > 1):
                raise ValueError("invalid numeric measurement")
        groups.setdefault(key, []).append(row)
    summary = []
    for key, samples in groups.items():
        item = dict(zip(["dataset", "method", "variant", "N", "M", "efConstruction", "E",
                         "efSearch", "probes", "leaf_capacity", "k"], key))
        item.update(profile=profile, samples=len(samples),
                    recall_pct=statistics.fmean(float(row["recall"]) for row in samples) * 100,
                    query_ms=statistics.fmean(float(row["query_ms"]) for row in samples),
                    verify_ms=statistics.fmean(float(row["verify_ms"]) for row in samples),
                    vo_KB=statistics.fmean(float(row["vo_bytes"]) for row in samples) / 1000,
                    route_proof_KB=(statistics.fmean(float(row["route_proof_bytes"]) for row in samples) / 1000
                                    if all(row["route_proof_bytes"] != "" for row in samples) else ""))
        summary.append(item)
    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / (args.experiment + ".csv")).open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    if args.experiment == "figure16":
        fig, axes = plt.subplots(1, 3, figsize=(11, 3.2))
        for axis, field, title in zip(axes, ["route_proof_KB", "vo_KB", "verify_ms"],
                                      ["Routing proof (KB)", "Total VO (KB)", "Verification (ms)"]):
            for variant in ["Record-wise", "Node-wise", "ER"]:
                points = sorted((row for row in summary if row["variant"] == variant), key=lambda row: int(row["M"]))
                if not points:
                    raise ValueError("missing authentication variant: " + variant)
                axis.plot([int(row["M"]) for row in points], [row[field] for row in points], marker="o", label=variant)
            axis.set_xlabel("M")
            axis.set_ylabel(title)
            axis.grid(alpha=0.25)
        axes[0].legend(frameon=False)
        if profile == "diagnostic":
            fig.suptitle("SIFT, N = " + str(summary[0]["N"]))
        fig.tight_layout()
        fig.savefig(args.output / "figure16.pdf")
        fig.savefig(args.output / "figure16.png", dpi=200)
        plt.close(fig)
    else:
        def tex(value):
            return str(value).replace("_", r"\_").replace("&", r"\&").replace("%", r"\%")
        lines = ["% Actual " + profile + " measurements; KB = 1000 bytes.",
                 r"\begin{tabular}{llrrrr}", r"Dataset & Method & R (\%) & Q (ms) & VO (KB) & V (ms) \\",
                 r"\hline"]
        for row in summary:
            lines.append(f"{tex(row['dataset'])} & {tex(row['method'])} & "
                         f"{row['recall_pct']:.1f} & {row['query_ms']:.3f} & "
                         f"{row['vo_KB']:.3f} & {row['verify_ms']:.3f} " + r"\\")
        lines.append(r"\end{tabular}")
        (args.output / "table4.tex").write_text("\n".join(lines) + "\n")
    sources = {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in args.input}
    (args.output / "sources.json").write_text(json.dumps({"profile": profile, "sources": sources}, indent=2) + "\n")
    print("Generated", args.output / (args.experiment + ".csv"))


if __name__ == "__main__":
    main()
