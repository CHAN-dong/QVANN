$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & wsl -d Ubuntu --exec python3 scripts/build_multidata_pilots.py
    if($LASTEXITCODE -ne 0){throw 'Calibration build failed.'}
    & wsl -d Ubuntu --exec python3 scripts/smoke_multidata_pilots.py
    if($LASTEXITCODE -ne 0){throw 'Calibration/full-protocol equivalence check failed.'}
    & wsl -d Ubuntu --exec python3 scripts/run_work9_multidata.py
    if($LASTEXITCODE -ne 0){throw 'Study stopped; inspect experiment logs before resuming.'}
    & wsl -d Ubuntu --exec python3 scripts/work9_multidata.py
    if($LASTEXITCODE -ne 0){throw 'Final workbook update failed.'}
    & wsl -d Ubuntu --exec python3 scripts/verify_work9_multidata.py
    if($LASTEXITCODE -ne 0){throw 'Final evidence audit failed.'}
    & wsl -d Ubuntu --exec python3 scripts/summarize_work9_multidata.py
    if($LASTEXITCODE -ne 0){throw 'Final result summary failed.'}
} finally { Pop-Location }
