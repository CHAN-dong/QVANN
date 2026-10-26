from pathlib import Path, PurePosixPath
import zipfile
root = Path(__file__).resolve().parent
with zipfile.ZipFile(root / 'upstream.zip') as archive:
    count = 0
    for entry in archive.infolist():
        parts = PurePosixPath(entry.filename).parts
        if not parts or '..' in parts or entry.is_dir():
            continue
        relative = Path(*parts[1:])
        if relative.suffix not in {'.cpp','.cc','.c','.h','.hpp','.py','.txt','.md','.cmake','.sol','.go'}:
            continue
        if any(p in {'build','dist','__pycache__','.git'} for p in relative.parts):
            continue
        if entry.file_size > 4_000_000:
            continue
        target = root / 'upstream' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(archive.read(entry))
        count += 1
print('Extracted source files:', count)
