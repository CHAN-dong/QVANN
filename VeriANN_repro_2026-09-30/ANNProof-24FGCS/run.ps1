param(
    [string]$Distribution='Ubuntu',[string]$Dataset='',[int]$Base=100000,[int]$Queries=100,
    [string]$K='10',[string]$EfSearch='50',[int]$M=8,[int]$EfConstruction=100,
    [int]$ShardSize=10000,[int]$BucketSize=20000,[string]$Probes='3',
    [double]$LshWidth=256,[int]$Seed=42,[int]$Repeats=1,
    [string]$Metric='l2',[string]$Proof='multiproof',[switch]$SkipBuild
)
$script=Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/run_paper_windows.ps1'
& $script -Method 'ANNProof-24FGCS' @PSBoundParameters
