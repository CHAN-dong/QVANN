"""Collect passing runs with their configuration, without mixing input sizes."""
import csv
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
rows = []
for method in ['ANNProof-24FGCS','VLAH-25TrustCom']:
    for folder in sorted((root/method/'results').glob('*')):
        if not (folder/'PASS.txt').exists():
            continue
        manifest = json.loads((folder/'manifest.json').read_text())
        with (folder/'summary.csv').open() as f:
            for record in csv.DictReader(f):
                record = dict(record)
                record.update({name:manifest[name] for name in ['base','queries','M','ef_construction',
                              'shard_size','bucket_size','lsh_width','seed','repeats','metric','proof','hash','dataset_sha256']})
                record['run_directory'] = str(folder.relative_to(root))
                rows.append(record)
output = root/'docs/paper_repro_all_runs.csv'
with output.open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(rows[0]) if rows else ['run_directory'])
    writer.writeheader()
    writer.writerows(rows)
print(f'Saved {len(rows)} configurations to {output}')
