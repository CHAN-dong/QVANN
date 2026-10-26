param([string]$Distribution='Ubuntu')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$linuxRoot=& wsl.exe -d $Distribution --exec wslpath -a $root.Replace('\','/')
if($LASTEXITCODE -ne 0){throw 'WSL path conversion failed'}
foreach($script in @('run_work9_ours.py','run_work9_ours_matched.py','test_ours_protocol.py','verify_work9_ours.py','summarize_work9_ours.py')){
    & wsl.exe -d $Distribution --cd $linuxRoot --exec python3 "scripts/$script"
    if($LASTEXITCODE -ne 0){throw "Failed: $script"}
}
