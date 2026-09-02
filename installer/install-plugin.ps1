param(
    [string]$VirtualDJHome = '',
    [string]$PayloadDirectory = '',
    [switch]$NonInteractive,
    [switch]$SkipProcessCheck
)

$ErrorActionPreference = 'Stop'
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptRoot 'installer-common.ps1')

Assert-VirtualDJClosed -SkipProcessCheck:$SkipProcessCheck
$VirtualDJHome = Resolve-VirtualDJHome -ExplicitPath $VirtualDJHome -NonInteractive:$NonInteractive
$PayloadDirectory = Resolve-LrcPayloadDirectory -ScriptRoot $ScriptRoot -ExplicitPath $PayloadDirectory

$PluginsRoot = Join-Path $VirtualDJHome 'Plugins64'
$OverlayDirectory = Join-Path $PluginsRoot 'VideoOverlay'
$VisualisationsDirectory = Join-Path $PluginsRoot 'Visualisations'
$VideoFxDirectory = Join-Path $PluginsRoot 'VideoEffect'
$VideoSourceDirectory = Join-Path $PluginsRoot 'VideoSource'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$BackupRoot = Join-Path $VirtualDJHome "LRC Lyrics Backups\$timestamp-before-install"
New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null

$logPath = Join-Path $VirtualDJHome 'LRC Lyrics Install.log'
$transcriptStarted = $false
try {
    Start-Transcript -LiteralPath $logPath -Append | Out-Null
    $transcriptStarted = $true
    Write-Host "VirtualDJ home: $VirtualDJHome"
    Write-Host "Plugin payload: $PayloadDirectory"

    $currentFiles = @(
        (Join-Path $OverlayDirectory 'LRCMaster.dll'),
        (Join-Path $OverlayDirectory 'LRCBlackOut.dll'),
        (Join-Path $OverlayDirectory 'LRC Master.dll'),
        (Join-Path $OverlayDirectory 'LRC BlackOut.dll'),
        (Join-Path $OverlayDirectory 'EmbeddedLyricsTagWriter.py')
    )
    foreach ($path in $currentFiles) {
        Copy-LrcBackupItem -Path $path -VirtualDJHome $VirtualDJHome -BackupRoot $BackupRoot
    }

    $legacyFiles = [System.Collections.Generic.List[string]]::new()
    foreach ($directory in @($VideoFxDirectory, $OverlayDirectory, $VisualisationsDirectory, $VideoSourceDirectory)) {
        foreach ($name in @(
            'EmbeddedLyricsDeck.dll', 'EmbeddedLyricsMaster.dll', 'Blackout.dll',
            'LRC Deck Basic.dll', 'LRC Master Basic.dll'
        )) {
            $legacyFiles.Add((Join-Path $directory $name))
        }
    }
    foreach ($name in @('LRC Master.dll', 'LRC BlackOut.dll')) {
        $legacyFiles.Add((Join-Path $OverlayDirectory $name))
    }
    foreach ($directory in @($VideoFxDirectory, $VisualisationsDirectory, $VideoSourceDirectory)) {
        $legacyFiles.Add((Join-Path $directory 'EmbeddedLyricsTagWriter.py'))
    }
    foreach ($name in @('LRC Deck.dll', 'LRC Master.dll', 'LRC BlackOut.dll', 'LRC Deck FX.dll')) {
        $legacyFiles.Add((Join-Path $VideoFxDirectory $name))
    }
    $legacyFiles.Add((Join-Path $VideoSourceDirectory 'LRC Deck.dll'))
    foreach ($name in @('LRC Deck.dll', 'LRC Deck.ini', 'LRC Deck_2.ini')) {
        $legacyFiles.Add((Join-Path $VisualisationsDirectory $name))
    }
    foreach ($name in @(
        'EmbeddedLyricsDeck.ini', 'EmbeddedLyricsMaster.ini', 'LRC Deck.ini',
        'LRC Deck_2.ini', 'LRC Deck FX.ini', 'LRC Master.ini'
    )) {
        $legacyFiles.Add((Join-Path $VideoFxDirectory $name))
    }

    foreach ($path in $legacyFiles | Select-Object -Unique) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Copy-LrcBackupItem -Path $path -VirtualDJHome $VirtualDJHome -BackupRoot $BackupRoot
            Remove-Item -LiteralPath $path -Force
            Write-Host "Removed legacy file: $path"
        }
    }

    $settingsPath = Join-Path $VirtualDJHome 'settings.xml'
    if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
        $settingsText = [System.IO.File]::ReadAllText($settingsPath)
        $selectedDeck = '<videoAudioOnlyVisualisation>LRC Deck</videoAudioOnlyVisualisation>'
        if ($settingsText.Contains($selectedDeck)) {
            Copy-LrcBackupItem -Path $settingsPath -VirtualDJHome $VirtualDJHome -BackupRoot $BackupRoot
            $settingsText = $settingsText.Replace(
                $selectedDeck,
                '<videoAudioOnlyVisualisation>None</videoAudioOnlyVisualisation>'
            )
            [System.IO.File]::WriteAllText($settingsPath, $settingsText, [System.Text.UTF8Encoding]::new($false))
            Write-Host 'Reset the obsolete LRC Deck audio-only source to None.'
        }
    }

    New-Item -ItemType Directory -Force -Path $OverlayDirectory | Out-Null
    Copy-LrcVerifiedFile -Source (Join-Path $PayloadDirectory 'LRCMaster.dll') -Destination (Join-Path $OverlayDirectory 'LRCMaster.dll')
    Copy-LrcVerifiedFile -Source (Join-Path $PayloadDirectory 'LRCBlackOut.dll') -Destination (Join-Path $OverlayDirectory 'LRCBlackOut.dll')
    Copy-LrcVerifiedFile -Source (Join-Path $PayloadDirectory 'EmbeddedLyricsTagWriter.py') -Destination (Join-Path $OverlayDirectory 'EmbeddedLyricsTagWriter.py')

    $version = Get-LrcVersion -ScriptRoot $ScriptRoot
    $manifest = [ordered]@{
        Version = $version
        InstalledAt = (Get-Date).ToString('o')
        VirtualDJHome = $VirtualDJHome
        Files = @(
            'Plugins64\VideoOverlay\LRCMaster.dll',
            'Plugins64\VideoOverlay\LRCBlackOut.dll',
            'Plugins64\VideoOverlay\EmbeddedLyricsTagWriter.py'
        )
    }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $VirtualDJHome 'LRC Lyrics Installation.json') -Encoding UTF8

    Write-Host ''
    Write-Host 'LRC Lyrics was installed successfully.' -ForegroundColor Green
    Write-Host "Installed into: $OverlayDirectory"
    Write-Host "Backup created at: $BackupRoot"
    Write-Host 'Start VirtualDJ and enable LRC Master and, when wanted, LRC BlackOut under Video Overlays.'
} finally {
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
}
