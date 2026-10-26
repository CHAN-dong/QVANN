#!/usr/bin/env python3
import argparse
from pathlib import Path
import numpy as np

def write_fvecs(path: Path, x: np.ndarray) -> None:
    x = np.asarray(x, dtype="<f4")
    rec = np.empty(x.shape[0], dtype=[("d", "<i4"), ("x", "<f4", (x.shape[1],))])
    rec["d"] = x.shape[1]
    rec["x"] = x
    rec.tofile(path)

def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--output", default="../dataset/hiergauss64")
    p.add_argument("--base", type=int, default=50000)
    p.add_argument("--learn", type=int, default=5000)
    p.add_argument("--query", type=int, default=1000)
    p.add_argument("--seed", type=int, default=20260830)
    a = p.parse_args()
    rng = np.random.default_rng(a.seed)
    d, nc, nm, nf = 64, 16, 16, 8
    # Orthogonal-ish hierarchy: coarse separation dominates middle, which
    # dominates fine and object noise.  The offset/range keep coordinates
    # compatible with the uint8-like certificate implementation.
    coarse = rng.normal(0, 22.0, size=(nc, d))
    middle = coarse[:, None, :] + rng.normal(0, 7.0, size=(nc, nm, d))
    fine = middle[:, :, None, :] + rng.normal(0, 2.2, size=(nc, nm, nf, d))
    fine = fine.reshape(nc * nm * nf, d)

    def sample(n: int) -> np.ndarray:
        ids = rng.integers(0, fine.shape[0], size=n)
        x = 128.0 + fine[ids] + rng.normal(0, 0.75, size=(n, d))
        return np.clip(np.rint(x), 0, 255).astype(np.float32)

    out = Path(a.output)
    out.mkdir(parents=True, exist_ok=True)
    write_fvecs(out / "sift_base.fvecs", sample(a.base))
    write_fvecs(out / "sift_learn.fvecs", sample(a.learn))
    write_fvecs(out / "sift_query.fvecs", sample(a.query))
    print(f"wrote {out}: base={a.base} learn={a.learn} query={a.query} d={d}")

if __name__ == "__main__":
    main()
