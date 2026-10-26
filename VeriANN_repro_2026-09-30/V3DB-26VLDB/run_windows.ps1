param(
    [ValidateSet('all', 'basic', 'smoke')][string]$Suite = 'all',
    [switch]$Build
)
$ErrorActionPreference = 'Stop'
$reproWindowsPath = $PSScriptRoot
$reproLinuxPath = (& wsl.exe --exec wslpath -a -u $reproWindowsPath | Select-Object -Last 1).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve project path in WSL.' }
if ($Build) {
    & wsl.exe --exec bash "$reproLinuxPath/scripts/build.sh"
    if ($LASTEXITCODE -ne 0) { throw 'V3DB build failed; inspect logs/build-pinned.log.' }
}
& wsl.exe --exec bash "$reproLinuxPath/scripts/run.sh" $Suite
if ($LASTEXITCODE -ne 0) { throw 'V3DB tests failed; inspect logs and results/suite.json.' }
