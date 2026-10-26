param(
    [Parameter(Mandatory=$true)][ValidateSet('ANNProof-24FGCS','VLAH-25TrustCom')][string]$Method,
    [string]$Distribution='Ubuntu', [string]$Dataset='',
    [int]$Base=100000, [int]$Queries=100, [string]$K='10', [string]$EfSearch='50',
    [int]$M=8, [int]$EfConstruction=100, [int]$ShardSize=10000,
    [int]$BucketSize=20000, [string]$Probes='3', [double]$LshWidth=256,
    [int]$Seed=42, [int]$Repeats=1,
    [ValidateSet('l2','cosine')][string]$Metric='l2',
    [ValidateSet('multiproof','paths')][string]$Proof='multiproof', [switch]$SkipBuild
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$folder=Join-Path $root $Method
$linuxFolder=& wsl.exe -d $Distribution -- wslpath -a $folder.Replace('\','/')
if($LASTEXITCODE -ne 0){throw 'WSL path conversion failed'}
$arguments=@('-d',$Distribution,'--cd',$linuxFolder,'--','python3','run.py',
    '--base',"$Base",'--queries',"$Queries",'--k',$K,'--ef-search',$EfSearch,
    '--M',"$M",'--ef-construction',"$EfConstruction",'--shard-size',"$ShardSize",
    '--bucket-size',"$BucketSize",'--probes',$Probes,'--lsh-width',"$LshWidth",
    '--seed',"$Seed",'--repeats',"$Repeats",'--metric',$Metric,'--proof',$Proof)
if($Dataset){$absolute=(Resolve-Path -LiteralPath $Dataset).Path;$linuxDataset=& wsl.exe -d $Distribution -- wslpath -a $absolute.Replace('\','/');if($LASTEXITCODE -ne 0){throw 'Dataset path conversion failed'};$arguments+=@('--dataset',$linuxDataset)}
if($SkipBuild){$arguments+='--skip-build'}
$ErrorActionPreference='Continue'
& wsl.exe @arguments
if($LASTEXITCODE -ne 0){throw "Paper reproduction failed (exit $LASTEXITCODE)"}
