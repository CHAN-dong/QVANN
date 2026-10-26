param([ValidateSet('run','fill','tune')][string]$Action='run')
$ErrorActionPreference='Stop'
$reproLinuxPath=(& wsl.exe --exec wslpath -a -u $PSScriptRoot | Select-Object -Last 1).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve project path in WSL.' }
$scriptName=switch($Action) {'fill' {'work9_fill.py'} 'tune' {'work9_tune.py'} default {'work9_run_all.py'}}
& wsl.exe --exec /root/.venvs/v3db-work9/bin/python "$reproLinuxPath/scripts/$scriptName"
if ($LASTEXITCODE -ne 0) { throw 'V3DB work9 operation failed; inspect experiments/work9-v3db-20261016 logs.' }
