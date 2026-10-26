# Recorded diagnostic run

These are actual measurements from the real first 5,000 SIFT vectors and query
IDs 100–101, one repeat, on the environment documented in `docs/validation.md`.
They validate the reproduction pipeline and **are not the paper's results**.

`raw/` preserves native CSVs and run manifests without changing their numeric
values. `input.json` records the real conversion inputs; no vectors or binary VOs
are redistributed. Absolute paths inside historical run manifests identify the
local execution environment and are not used by the scripts as defaults.

Generate the diagnostic table and figure from these recorded samples:

```bash
python scripts/plot.py table3 --input validation/raw/table3/measurements.csv --output results/recorded-table3
python scripts/plot.py figure16 --input validation/raw/figure16/measurements.csv --output results/recorded-figure16
```

The plotting script verifies the retained source CSV hashes. A fresh execution
requires real data and `scripts/reproduce.py`; plotting this snapshot does not
constitute a fresh experiment.
