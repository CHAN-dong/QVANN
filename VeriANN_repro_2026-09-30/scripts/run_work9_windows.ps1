param([string]$Distribution='Ubuntu',[switch]$UpdateOriginal)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$linuxRoot=& wsl.exe -d $Distribution --exec wslpath -a $root.Replace('\','/')
if($LASTEXITCODE -ne 0){throw 'WSL path conversion failed'}
foreach($phase in @('sift','size')){
    & wsl.exe -d $Distribution --cd $linuxRoot --exec python3 scripts/run_work9_experiments.py --phase $phase
    if($LASTEXITCODE -ne 0){throw "Experiment phase failed: $phase"}
}
$arguments=@('-d',$Distribution,'--cd',$linuxRoot,'--exec','python3','scripts/fill_work9.py')
if($UpdateOriginal){$arguments+='--update-original'}
& wsl.exe @arguments
if($LASTEXITCODE -ne 0){throw 'Workbook export failed'}
& wsl.exe -d $Distribution --cd $linuxRoot --exec python3 scripts/summarize_work9.py
if($LASTEXITCODE -ne 0){throw 'Results report failed'}
if($UpdateOriginal){
    & wsl.exe -d $Distribution --cd $linuxRoot --exec python3 scripts/verify_work9.py
    if($LASTEXITCODE -ne 0){throw 'Final workbook verification failed'}
}
