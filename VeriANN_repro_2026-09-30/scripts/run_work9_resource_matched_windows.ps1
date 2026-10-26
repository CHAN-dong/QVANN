param([ValidateSet('start','run','fill','validate')][string]$Action='start')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & wsl --exec /root/.venvs/v3db-work9/bin/python scripts/work9_resource_matched.py $Action
    if($LASTEXITCODE -ne 0){throw 'Resource-matched study failed; inspect experiments/work9-resource-matched-20261020/runner_failure.json.'}
} finally { Pop-Location }
