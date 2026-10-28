# Example output

SIFT prefix: 5,000 vectors, query rows 100–101, one repeat. `raw/` contains the
per-query CSVs and run parameters. `input.json` records input checksums.

```bash
python scripts/plot.py table4 --input validation/raw/table3/measurements.csv --output results/example-table4
python scripts/plot.py figure16 --input validation/raw/figure16/measurements.csv --output results/example-figure16
```

Use `scripts/compare.py` and `scripts/reproduce.py` to create new runs.
