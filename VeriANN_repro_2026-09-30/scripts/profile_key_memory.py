#!/usr/bin/env python3
"""Measure actual SIFT100K peak RSS without touching existing experiment CSVs."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from datetime import datetime

root = Path(__file__).resolve().parents[1]
output = root / 'results' / ('memory-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
output.mkdir()
env = os.environ.copy()
env['OMP_NUM_THREADS'] = env['OPENBLAS_NUM_THREADS'] = '1'
env['LD_LIBRARY_PATH'] = ':'.join(str(p) for p in (
    root / 'third_party/runtime/lib', root / 'third_party/LearnedEarlyTer-20SIGMOD',
    root / 'third_party/LearnedEarlyTer-20SIGMOD/lib'))
measurements = []
with tempfile.TemporaryDirectory(prefix='.memory-run-', dir=root) as temporary:
    directory = Path(temporary)
    for name in ('dataset', 'merkle_hnsw'):
        (directory / name).symlink_to(root / name, target_is_directory=True)
    work = directory / 'work'
    work.mkdir()
    for name in ('e_budget_relaxed_hnsw', 'expansion_centric_merkle', 'refinement_merkle_ordering'):
        stats = output / (name + '.rss')
        started = time.perf_counter()
        with (output / (name + '.log')).open('w') as log:
            subprocess.run(['/usr/bin/time', '-f', '%M', '-o', str(stats),
                            str(root / 'build' / (name + '_experiment'))],
                           cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        kib = int(stats.read_text().strip())
        row = {'experiment': name, 'base_vectors': 100000, 'peak_rss_kib': kib,
               'peak_rss_mib': round(kib / 1024, 2),
               'elapsed_seconds': round(time.perf_counter() - started, 3)}
        measurements.append(row)
        print(json.dumps(row), flush=True)
(output / 'measurements.json').write_text(json.dumps(measurements, indent=2) + '\n')
print('Saved: ' + str(output))
