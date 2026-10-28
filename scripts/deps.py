#!/usr/bin/env python3
"""Build the pinned CPU dependencies."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    print("+", " ".join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=ROOT / ".deps")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--source-root", type=Path,
                        help="optional directory with clean faiss/ and LightGBM/ checkouts")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    prefix = args.prefix.resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / "deps/versions.json").read_text())
    trees = {}
    for name, folder in (("faiss", "LearnedEarlyTer-20SIGMOD"), ("lightgbm", "LightGBM")):
        spec = lock[name]
        target = prefix / folder
        if not target.exists():
            source = args.source_root / ("faiss" if name == "faiss" else "LightGBM") if args.source_root else spec["url"]
            run(["git", "init", target])
            run(["git", "-C", target, "remote", "add", "origin", source])
            run(["git", "-C", target, "fetch", "--depth", "1", "origin", spec["commit"]])
            run(["git", "-C", target, "checkout", "--detach", spec["commit"]])
        actual = subprocess.check_output(["git", "-C", str(target), "rev-parse", "HEAD"], text=True).strip()
        if actual != spec["commit"]:
            raise RuntimeError(f"{target}: expected {spec['commit']}, got {actual}")
        trees[name] = target

    faiss = trees["faiss"]
    patch = ROOT / "deps/faiss.patch"
    reverse = subprocess.run(["git", "-C", str(faiss), "apply", "--reverse", "--check", str(patch)],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if reverse.returncode:
        run(["git", "-C", faiss, "apply", "--check", patch])
        run(["git", "-C", faiss, "apply", patch])
    for name, expected in lock["faiss"].get("patched_files", {}).items():
        actual = hashlib.sha256((faiss / name).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError("patched dependency source differs: " + name)

    lightgbm = trees["lightgbm"]
    run(["cmake", "-S", lightgbm, "-B", lightgbm / "build", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", "-DCMAKE_CXX_FLAGS=-include cstdint"])
    run(["cmake", "--build", lightgbm / "build", "--target", "_lightgbm", "-j", args.jobs])
    built = lightgbm / "lib_lightgbm.so"
    if not built.is_file():
        raise RuntimeError("LightGBM did not produce lib_lightgbm.so")
    (faiss / "lib").mkdir(exist_ok=True)
    shutil.copy2(built, faiss / "lib/lib_lightgbm.so")
    # Explicit make variables replace upstream machine-specific configure paths.
    # Always rebuild objects so stale compiler flags cannot select bundled binaries.
    run(["make", "-C", faiss, "clean"])
    run(["make", "-C", faiss, "-j", args.jobs, "libfaiss.so",
         "CXX=" + os.environ.get("CXX", "g++") + " -std=c++11",
         "CPPFLAGS=-DFINTEGER=int -fopenmp -include cstdint -Iinclude",
         "CXXFLAGS=-O3 -DNDEBUG -fPIC -Wall -Wextra -Wno-sign-compare",
         "CPUFLAGS=-msse4 -mpopcnt", "LDFLAGS=-fopenmp",
         "LIBS=-lopenblas", "SHAREDFLAGS=-shared", "SHAREDEXT=so"])
    manifest = {"sources": lock, "patch_sha256": hashlib.sha256(patch.read_bytes()).hexdigest(),
                "compiler": subprocess.check_output([os.environ.get("CXX", "g++"), "--version"], text=True),
                "cmake": subprocess.check_output(["cmake", "--version"], text=True)}
    for name, file in (("faiss", faiss / "libfaiss.so"), ("lightgbm", faiss / "lib/lib_lightgbm.so")):
        manifest[name + "_sha256"] = hashlib.sha256(file.read_bytes()).hexdigest()
    (prefix / "build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Dependencies built from source. Configure with -DQVANN_DEPS_DIR=" + str(prefix))


if __name__ == "__main__":
    main()
