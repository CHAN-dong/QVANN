param([string]$Distribution = 'Ubuntu', [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root ('results/windows-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
function Invoke-Linux([string]$Command, [string]$LogName) {
    # Windows PowerShell represents native stderr as ErrorRecord objects.
    # Preserve those log lines; determine failure from the process exit code.
    $ErrorActionPreference = 'Continue'
    & wsl.exe -d $Distribution -- bash -lc $Command 2>&1 | Tee-Object -FilePath (Join-Path $output $LogName)
    if ($LASTEXITCODE -ne 0) { throw "WSL command failed (exit $LASTEXITCODE). See $output/$LogName" }
}
# Resolve Windows paths through wslpath; quote paths for Bash, including apostrophes.
function Quote-Bash([string]$Value) {
    $quote = "'"
    $escaped = $quote + '"' + $quote + '"' + $quote
    return $quote + $Value.Replace($quote, $escaped) + $quote
}
$linuxRoot = & wsl.exe -d $Distribution -- wslpath -a $root.Replace('\', '/')
if ($LASTEXITCODE -ne 0 -or -not $linuxRoot) {
    'FAIL: Ubuntu/WSL is unavailable; no experiment was started.' | Set-Content (Join-Path $output 'environment.log')
    throw "No usable WSL distribution '$Distribution'. Install WSL/Ubuntu first. Output directory: $output"
}
$linuxOutput = & wsl.exe -d $Distribution -- wslpath -a $output.Replace('\', '/')
if ($LASTEXITCODE -ne 0) { throw 'Cannot convert output path with wslpath' }
$prefix = 'cd ' + (Quote-Bash $linuxRoot) + ' && '
Invoke-Linux ($prefix + 'uname -a && command -v g++ && command -v python3 && command -v bash') 'environment.log'
if (-not $SkipBuild) { Invoke-Linux ($prefix + 'bash scripts/build_key_experiments.sh') 'build.log' }
Invoke-Linux ($prefix + 'OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 VANN_OUTPUT_DIR=' + (Quote-Bash $linuxOutput) + ' bash scripts/run_key_experiments.sh') 'run.log'
Invoke-Linux ($prefix + 'python3 scripts/compare_key_results.py --actual ' + (Quote-Bash $linuxOutput)) 'compare.log'
Write-Host "PASS: fresh experiments match references. Logs and CSV: $output"
