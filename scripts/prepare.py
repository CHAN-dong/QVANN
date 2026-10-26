#!/usr/bin/env python3
"""Convert real source vectors to QVANN fvecs, recording preprocessing and hashes."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import h5py
import numpy as np

DIMENSIONS = {"sift": 128, "nytimes": 256, "glove": 100, "deep": 96}


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def convert(source, output, normalize, limit, drop_zero, preserve_zero=False):
    count = min(len(source), limit) if limit else len(source)
    dimension = source.shape[1]
    rows = dropped = 0
    zero_rows, original_ids = [], []
    payload = hashlib.sha256()
    with output.open("xb") as stream:
        for start in range(0, count, 8192):
            values = np.asarray(source[start:min(start + 8192, count)], dtype="<f4").copy()
            if not np.all(np.isfinite(values)):
                raise ValueError("source contains nonfinite coordinates")
            if normalize:
                norms = np.sqrt(np.sum(values.astype(np.float64) ** 2, axis=1))
                zero = norms == 0
                if np.any(zero):
                    if not drop_zero and not preserve_zero:
                        raise ValueError("zero query vector cannot be normalized")
                    zero_rows.extend((start + np.flatnonzero(zero)).tolist())
                    if drop_zero:
                        dropped += int(zero.sum())
                        original_ids.extend((start + np.flatnonzero(~zero)).tolist())
                        values, norms = values[~zero], norms[~zero]
                    else:
                        norms[zero] = 1
                elif drop_zero:
                    original_ids.extend(range(start, start + len(values)))
                values *= np.asarray(1 / norms, dtype="<f4")[:, None]
            payload.update(values.tobytes())
            records = np.empty((len(values), dimension + 1), dtype="<f4")
            records.view("<i4")[:, 0] = dimension
            records[:, 1:] = values
            stream.write(records.tobytes())
            rows += len(values)
    if drop_zero:
        np.save(output.parent / "base_original_ids.npy", np.asarray(original_ids, dtype="<i8"))
    return {"rows": rows, "dimension": dimension, "zero_rows_removed": dropped,
            "source_zero_rows": zero_rows, "zero_rows_preserved": preserve_zero,
            "file_sha256": sha(output), "payload_sha256": payload.hexdigest()}


def fbin(path, count, dimension):
    with path.open("rb") as stream:
        declared, d = struct.unpack("<ii", stream.read(8))
    if d != dimension or declared < count:
        raise ValueError("invalid fbin header or requested prefix exceeds source")
    if path.stat().st_size < 8 + count * d * 4:
        raise ValueError("truncated requested fbin prefix")
    return np.memmap(path, dtype="<f4", mode="r", offset=8, shape=(count, d))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", choices=DIMENSIONS, required=True)
    parser.add_argument("--source", type=Path, required=True, help="HDF5, or DEEP base fbin")
    parser.add_argument("--queries", type=Path, help="DEEP query fbin")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--base", type=int, help="optional diagnostic subset, not a paper run")
    args = parser.parse_args()
    if args.base is not None and args.base < 1:
        parser.error("--base must be positive")
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("output directory must be new or empty")
    args.output.mkdir(parents=True, exist_ok=True)
    if args.dataset == "deep" and args.queries is None:
        parser.error("DEEP requires --queries")
    manifest = {"dataset": args.dataset, "source": str(args.source.resolve()),
                "source_sha256": sha(args.source), "normalized": args.dataset in ("nytimes", "glove"),
                "diagnostic_subset": args.base is not None}
    if args.dataset == "deep":
        base = fbin(args.source, args.base or 1000000, 96)
        with args.queries.open("rb") as stream:
            n, d = struct.unpack("<ii", stream.read(8))
        query = fbin(args.queries, n, 96)
        manifest["query_source_sha256"] = sha(args.queries)
        sources = {"train": base, "test": query}
        handle = None
    else:
        handle = h5py.File(args.source, "r")
        sources = handle
    try:
        for key, filename in (("train", "sift_base.fvecs"), ("test", "sift_query.fvecs")):
            if sources[key].shape[1] != DIMENSIONS[args.dataset]:
                raise ValueError("unexpected dataset dimension")
            manifest[key] = convert(sources[key], args.output / filename, manifest["normalized"],
                                    args.base if key == "train" else None,
                                    args.dataset == "nytimes" and key == "train",
                                    args.dataset == "nytimes" and key == "test")
    finally:
        if handle is not None:
            handle.close()
    (args.output / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
