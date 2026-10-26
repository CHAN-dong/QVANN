import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys
from work9_calibrate import ROOT,REPRO,STUDY,DATA
import zk_IVF_PQ.zk_IVF_PQ as extension

def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

differences=[]
for original in (REPRO/'upstream/src').rglob('*.rs'):
    relative=original.relative_to(REPRO/'upstream')
    if digest(original)!=digest(REPRO/'work9-author'/relative):differences.append(str(relative))
assert set(differences)=={'src/lib.rs','src/utils/metrics.rs','src/ivf_pq_verify/proof.rs'}
paths=[REPRO/'work9-adapter.patch',Path(extension.__file__)]
paths += list((REPRO/'scripts').glob('work9_*.py'))
paths += [REPRO/'scripts/build_work9_adapter.py',REPRO/'work9-author/Cargo.lock']
records={str(p):dict(sha256=digest(p),bytes=p.stat().st_size) for p in paths}
provenance={}
for key in list(DATA)+['bigann10m','deep10m']:
    path=ROOT/'dataset'/key/'provenance.json'
    if path.exists():provenance[key]=json.loads(path.read_text())
result=dict(python=sys.version,platform=platform.platform(),
            author_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPRO/'upstream',text=True).strip(),
            modified_rust_files=differences,circuit_gadget_sources_unchanged=True,
            fingerprints=records,input_provenance=provenance)
(STUDY/'environment.json').write_text(json.dumps(result,indent=2)+'\n')
freeze=subprocess.check_output([sys.executable,'-m','pip','freeze'],text=True)
(REPRO/'requirements-work9.lock').write_text('\n'.join(x for x in freeze.splitlines() if not x.startswith('-e '))+'\n')
print(json.dumps(dict(files=len(records),modified_rust_files=differences,circuit_gadget_sources_unchanged=True)))
