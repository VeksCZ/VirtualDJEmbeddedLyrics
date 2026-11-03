<#
Keeps helper/ in step with its source of truth, lyricstools/ in the DJ Music Manager repository.
  ./sync-from-manager.ps1            copy the four shared files from the manager checkout
  ./sync-from-manager.ps1 -Check     only compare; exit code 1 when a file differs
The manager checkout is looked up next to this repository, or given with -ManagerPath.
#>
param(
    [string]$ManagerPath = '',
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$files = 'lrc_helper.py', 'lrc_tool.py', 'lyrics_lookup.py', 'lyrics_tag_converter.py'

if (-not $ManagerPath) {
    $candidates = @(
        (Join-Path (Split-Path -Parent $root) 'DJMusicManager'),
        (Join-Path $env:USERPROFILE 'source\repos\DJMusicManager'),
        (Join-Path $env:USERPROFILE 'DJMusicManager'))
    $ManagerPath = $candidates | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'lyricstools') } | Select-Object -First 1
}
if (-not $ManagerPath) { throw 'DJ Music Manager checkout not found. Pass -ManagerPath.' }
$source = Join-Path $ManagerPath 'lyricstools'

$different = @()
foreach ($name in $files) {
    $from = Join-Path $source $name
    $to = Join-Path $root "helper\$name"
    if (-not (Test-Path -LiteralPath $from)) { throw "Missing in the manager: $from" }
    $same = (Test-Path -LiteralPath $to) -and
        ((Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash)
    if ($same) { continue }
    $different += $name
    if (-not $Check) { Copy-Item -LiteralPath $from -Destination $to -Force }
}
if ($different.Count -eq 0) { Write-Host 'helper/ matches the manager.' -ForegroundColor Green; exit 0 }
if ($Check) {
    Write-Host ("helper/ differs from the manager: " + ($different -join ', ')) -ForegroundColor Yellow
    exit 1
}
Write-Host ("Updated from the manager: " + ($different -join ', ')) -ForegroundColor Green
