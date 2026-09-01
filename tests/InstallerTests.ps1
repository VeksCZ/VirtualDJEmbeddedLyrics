param([Parameter(Mandatory)][string]$PackageDirectory)

$ErrorActionPreference = 'Stop'
$PackageDirectory = [System.IO.Path]::GetFullPath($PackageDirectory)
$PayloadDirectory = Join-Path $PackageDirectory 'Plugins'
$TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("LRC Installer Test 日本語 " + [guid]::NewGuid().ToString('N'))
$VirtualDJTestHome = Join-Path $TestRoot 'Custom VirtualDJ Home'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "Installer test failed: $Message" }
}

try {
    New-Item -ItemType Directory -Force -Path (Join-Path $VirtualDJTestHome 'Plugins64\VideoOverlay') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $VirtualDJTestHome 'Plugins64\VideoEffect') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $VirtualDJTestHome 'Plugins64\Visualisations') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $VirtualDJTestHome 'Plugins64\VideoSource') | Out-Null
    Set-Content -LiteralPath (Join-Path $VirtualDJTestHome 'settings.xml') -Value '<settings><videoAudioOnlyVisualisation>LRC Deck</videoAudioOnlyVisualisation></settings>' -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $VirtualDJTestHome 'Plugins64\VideoEffect\LRC Deck FX.dll') -Value 'legacy' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $VirtualDJTestHome 'Plugins64\Visualisations\LRC Deck.dll') -Value 'legacy' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $VirtualDJTestHome 'Plugins64\VideoOverlay\LRC Master.dll') -Value 'old' -Encoding ASCII

    & (Join-Path $PackageDirectory 'install-plugin.ps1') -VirtualDJHome $VirtualDJTestHome -PayloadDirectory $PayloadDirectory -NonInteractive -SkipProcessCheck
    foreach ($name in @('LRC Master.dll', 'LRC BlackOut.dll', 'EmbeddedLyricsTagWriter.py')) {
        $installed = Join-Path $VirtualDJTestHome "Plugins64\VideoOverlay\$name"
        Assert-True (Test-Path -LiteralPath $installed -PathType Leaf) "$name was not installed"
        $source = Join-Path $PayloadDirectory $name
        Assert-True ((Get-FileHash -LiteralPath $installed).Hash -eq (Get-FileHash -LiteralPath $source).Hash) "$name hash differs"
    }
    $toolsDirectory = Join-Path $PackageDirectory 'Tools'
    foreach ($name in @(
        'MP3 & Lyrics Tools.cmd', 'MP3 & Lyrics Tools Silent.vbs',
        'Import Lyrics.cmd', 'Mark Existing Lyrics.cmd', 'lyrics_tools_gui.py',
        'lyrics_tag_converter.py', 'lrc_tool.py', 'restore_lrc.py',
        'embed_and_backup.py', 'requirements.txt', 'README.md'
    )) {
        Assert-True (Test-Path -LiteralPath (Join-Path $toolsDirectory $name) -PathType Leaf) "Tools/$name is missing"
    }
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $VirtualDJTestHome 'Plugins64\VideoEffect\LRC Deck FX.dll'))) 'legacy Deck FX remains'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $VirtualDJTestHome 'Plugins64\Visualisations\LRC Deck.dll'))) 'legacy visualisation remains'
    Assert-True ((Get-Content -LiteralPath (Join-Path $VirtualDJTestHome 'settings.xml') -Raw).Contains('<videoAudioOnlyVisualisation>None</videoAudioOnlyVisualisation>')) 'obsolete audio-only selection was not reset'
    Assert-True ((Get-ChildItem -LiteralPath (Join-Path $VirtualDJTestHome 'LRC Lyrics Backups') -Recurse -File).Count -ge 3) 'upgrade backup is incomplete'
    $preInstallBackup = Get-ChildItem -LiteralPath (Join-Path $VirtualDJTestHome 'LRC Lyrics Backups') -Directory | Sort-Object Name | Select-Object -First 1

    & (Join-Path $PackageDirectory 'install-plugin.ps1') -VirtualDJHome $VirtualDJTestHome -PayloadDirectory $PayloadDirectory -NonInteractive -SkipProcessCheck

    & (Join-Path $PackageDirectory 'uninstall-plugin.ps1') -VirtualDJHome $VirtualDJTestHome -NonInteractive -SkipProcessCheck
    foreach ($name in @('LRC Master.dll', 'LRC BlackOut.dll', 'EmbeddedLyricsTagWriter.py')) {
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $VirtualDJTestHome "Plugins64\VideoOverlay\$name"))) "$name remains after uninstall"
    }
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $PayloadDirectory 'LRC Deck.dll'))) 'release payload contains LRC Deck'

    & (Join-Path $PackageDirectory 'restore-backup.ps1') -VirtualDJHome $VirtualDJTestHome -BackupDirectory $preInstallBackup.FullName -NonInteractive -SkipProcessCheck
    Assert-True ((Get-Content -LiteralPath (Join-Path $VirtualDJTestHome 'Plugins64\VideoOverlay\LRC Master.dll') -Raw).Contains('old')) 'previous master plugin was not restored'
    Assert-True (Test-Path -LiteralPath (Join-Path $VirtualDJTestHome 'Plugins64\VideoEffect\LRC Deck FX.dll')) 'legacy file from backup was not restored'
    Assert-True ((Get-Content -LiteralPath (Join-Path $VirtualDJTestHome 'settings.xml') -Raw).Contains('<videoAudioOnlyVisualisation>LRC Deck</videoAudioOnlyVisualisation>')) 'previous settings were not restored'
    Write-Host 'Installer integration tests passed.' -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $TestRoot) {
        $resolvedTestRoot = [System.IO.Path]::GetFullPath($TestRoot)
        $temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if (-not $resolvedTestRoot.StartsWith($temporaryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove test data outside the temporary directory: $resolvedTestRoot"
        }
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
