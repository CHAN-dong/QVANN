$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$folder=Join-Path $root 'dataset/bigann10m'
$parts=Join-Path $folder 'parts'
New-Item -ItemType Directory -Force -Path $parts | Out-Null
$total=1280000008L
$chunk=67108864L
$url='https://dl.fbaipublicfiles.com/billion-scale-ann-benchmarks/bigann/base.1B.u8bin'
$blocks=@()
$items=@()
$number=0
for($begin=0L;$begin -lt $total;$begin+=$chunk){
    $end=[Math]::Min($begin+$chunk-1,$total-1)
    $path=Join-Path $parts ('part-{0:D3}.bin' -f $number)
    $header=Join-Path $parts ('part-{0:D3}.headers' -f $number)
    $items+=@{Path=$path;Header=$header;Begin=$begin;End=$end;Length=$end-$begin+1}
    $number++
    if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -eq ($end-$begin+1)){continue}
    $block=@(
        ('url = "'+$url+'"'),
        ('range = "'+$begin+'-'+$end+'"'),
        ('output = "'+$path.Replace('\','/')+'"'),
        ('dump-header = "'+$header.Replace('\','/')+'"'),
        ('max-filesize = '+($end-$begin+1)),
        'proxy = "http://127.0.0.1:7897"',
        'location','fail','retry = 2','max-time = 600'
    ) -join "`n"
    if(($block -split "`n").Count -ne 10){throw 'Invalid curl configuration line count'}
    $blocks+=$block
}
if($blocks.Count){
    $config=Join-Path $parts 'requests.curl'
    [System.IO.File]::WriteAllText($config,($blocks -join "`nnext`n")+"`n")
    $savedPreference=$ErrorActionPreference
    $ErrorActionPreference='Continue'
    & curl.exe --parallel --parallel-immediate --parallel-max 8 --config $config 1> (Join-Path $parts 'curl.stdout.log') 2> (Join-Path $parts 'curl.stderr.log')
    $status=$LASTEXITCODE
    $ErrorActionPreference=$savedPreference
    if($status -ne 0){throw "BIGANN chunk download failed: $status; see parts/curl.stderr.log"}
}
$etag=$null
foreach($item in $items){
    if((Get-Item -LiteralPath $item.Path).Length -ne $item.Length){throw 'Wrong chunk length'}
    $headers=[System.IO.File]::ReadAllText($item.Header)
    $expected='Content-Range: bytes '+$item.Begin+'-'+$item.End+'/128000000008'
    if($headers -notmatch [regex]::Escape($expected)){throw 'Wrong HTTP range response'}
    $match=[regex]::Match($headers,'(?im)^ETag:\s*(.+)$')
    $current=$match.Groups[1].Value.Trim()
    if(-not $current){throw 'Missing source ETag'}
    if($etag -and $etag -ne $current){throw 'Source changed between chunks'}
    $etag=$current
}
$temporary=Join-Path $folder 'base.10M.merged.u8bin'
$output=[System.IO.File]::Create($temporary)
try{foreach($item in $items){$partStream=[System.IO.File]::OpenRead($item.Path);try{$partStream.CopyTo($output)}finally{$partStream.Dispose()}}}finally{$output.Dispose()}
if((Get-Item -LiteralPath $temporary).Length -ne $total){throw 'Merged prefix length mismatch'}
Move-Item -LiteralPath $temporary -Destination (Join-Path $folder 'base.10M.prefix.u8bin') -Force
& curl.exe -L --fail --retry 2 --max-time 120 --max-filesize 1280008 --proxy http://127.0.0.1:7897 'https://dl.fbaipublicfiles.com/billion-scale-ann-benchmarks/bigann/query.public.10K.u8bin' -o (Join-Path $folder 'query.public.10K.u8bin')
if($LASTEXITCODE -ne 0){throw 'Query download failed'}
Write-Output "PASS: official BIGANN 10M prefix downloaded; all byte ranges and source ETags checked."
