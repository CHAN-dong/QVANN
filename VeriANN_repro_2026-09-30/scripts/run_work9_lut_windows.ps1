param([string]$Distribution='Ubuntu')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$linuxRoot=& wsl.exe -d $Distribution --exec wslpath -a $root.Replace('\','/')
if($LASTEXITCODE -ne 0){throw 'WSL path conversion failed'}
foreach($script in @('run_work9_lut.py','verify_work9_lut.py','summarize_work9_lut.py')){
    & wsl.exe -d $Distribution --cd $linuxRoot --exec python3 "scripts/$script"
    if($LASTEXITCODE -ne 0){throw "Failed: $script"}
}
