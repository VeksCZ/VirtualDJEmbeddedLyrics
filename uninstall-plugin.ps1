param(
    [string]$VirtualDJHome = '',
    [switch]$NonInteractive,
    [switch]$SkipProcessCheck
)

$ErrorActionPreference = 'Stop'
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptRoot 'installer-common.ps1')

Assert-VirtualDJClosed -SkipProcessCheck:$SkipProcessCheck
$VirtualDJHome = Resolve-VirtualDJHome -ExplicitPath $VirtualDJHome -NonInteractive:$NonInteractive
$OverlayDirectory = Join-Path (Join-Path $VirtualDJHome 'Plugins64') 'VideoOverlay'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$BackupRoot = Join-Path $VirtualDJHome "LRC Lyrics Backups\$timestamp-before-uninstall"
New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null

$removed = 0
foreach ($name in @('LRC Master.dll', 'LRC BlackOut.dll', 'EmbeddedLyricsTagWriter.py')) {
    $path = Join-Path $OverlayDirectory $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        Copy-LrcBackupItem -Path $path -VirtualDJHome $VirtualDJHome -BackupRoot $BackupRoot
        Remove-Item -LiteralPath $path -Force
        Write-Host "Removed: $path"
        ++$removed
    }
}
$manifestPath = Join-Path $VirtualDJHome 'LRC Lyrics Installation.json'
if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    Copy-LrcBackupItem -Path $manifestPath -VirtualDJHome $VirtualDJHome -BackupRoot $BackupRoot
    Remove-Item -LiteralPath $manifestPath -Force
}

if ($removed -eq 0) {
    Write-Host 'No installed LRC Lyrics plugin files were found.' -ForegroundColor Yellow
} else {
    Write-Host ''
    Write-Host 'LRC Lyrics was uninstalled successfully.' -ForegroundColor Green
    Write-Host "Backup created at: $BackupRoot"
}
