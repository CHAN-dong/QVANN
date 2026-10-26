$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & wsl -d Ubuntu --exec python3 scripts/download_extra_datasets.py
    if($LASTEXITCODE -ne 0){throw 'Dataset download failed; rerun to resume verified chunks.'}
    & wsl -d Ubuntu --exec python3 scripts/prepare_extra_datasets.py
    if($LASTEXITCODE -ne 0){throw 'Dataset conversion/validation failed.'}
    & wsl -d Ubuntu --exec python3 scripts/verify_extra_datasets.py
    if($LASTEXITCODE -ne 0){throw 'Prepared dataset integrity verification failed.'}
} finally { Pop-Location }
