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

    $detected = & (Join-Path $PackageDirectory 'detect-vdj-home.ps1') -ExplicitPath $VirtualDJTestHome | ConvertFrom-Json
    Assert-True ($detected.Valid -and $detected.Selected -eq $VirtualDJTestHome) 'GUI path detector rejected the test VirtualDJ home'

    & (Join-Path $PackageDirectory 'install-plugin.ps1') -VirtualDJHome $VirtualDJTestHome -PayloadDirectory $PayloadDirectory -NonInteractive -SkipProcessCheck
    foreach ($name in @('LRCMaster.dll', 'LRCBlackOut.dll', 'EmbeddedLyricsTagWriter.py')) {
        $installed = Join-Path $VirtualDJTestHome "Plugins64\VideoOverlay\$name"
        Assert-True (Test-Path -LiteralPath $installed -PathType Leaf) "$name was not installed"
        $source = Join-Path $PayloadDirectory $name
        Assert-True ((Get-FileHash -LiteralPath $installed).Hash -eq (Get-FileHash -LiteralPath $source).Hash) "$name hash differs"
    }
    Assert-True (Test-Path -LiteralPath (Join-Path $PackageDirectory 'LyricsTools.cmd') -PathType Leaf) 'root GUI launcher is missing'
    $toolsDirectory = Join-Path $PackageDirectory 'Tools'
    Assert-True ((Get-ChildItem -LiteralPath $toolsDirectory -Include '*.cmd', '*.bat', '*.vbs' -File).Count -eq 0) 'Tools contains a duplicate launcher'
    foreach ($name in @(
        'lyrics_tools_gui.py',
        'lyrics_tag_converter.py', 'lrc_tool.py', 'restore_lrc.py', 'vdj_setup.py',
        'vdj_playlist_sync.py', 'requirements.txt', 'README.md'
    )) {
        Assert-True (Test-Path -LiteralPath (Join-Path $toolsDirectory $name) -PathType Leaf) "Tools/$name is missing"
    }

    $mockBin = Join-Path $TestRoot 'mock-bin'
    $launcherLog = Join-Path $TestRoot 'launcher.log'
    New-Item -ItemType Directory -Force -Path $mockBin | Out-Null
    Set-Content -LiteralPath (Join-Path $mockBin 'python.cmd') -Encoding ASCII -Value @(
        '@echo off',
        'echo %*>>"%LYRICS_LAUNCH_LOG%"',
        'exit /b 0'
    )
    $previousPath = $env:PATH
    $previousLauncherLog = $env:LYRICS_LAUNCH_LOG
    try {
        $env:PATH = "$mockBin;$previousPath"
        $env:LYRICS_LAUNCH_LOG = $launcherLog
        Push-Location $PackageDirectory
        try {
            & cmd.exe /d /c 'LyricsTools.cmd'
            Assert-True ($LASTEXITCODE -eq 0) 'root GUI launcher returned an error'
        } finally {
            Pop-Location
        }
    } finally {
        $env:PATH = $previousPath
        $env:LYRICS_LAUNCH_LOG = $previousLauncherLog
    }
    $launcherCalls = Get-Content -LiteralPath $launcherLog
    Assert-True ($launcherCalls.Count -eq 2) 'root GUI launcher did not invoke dependency check and GUI'
    Assert-True ($launcherCalls[-1].ToLowerInvariant().Contains('tools\lyrics_tools_gui.py')) 'root GUI launcher did not invoke the bundled GUI'

    Assert-True (-not (Test-Path -LiteralPath (Join-Path $VirtualDJTestHome 'Plugins64\VideoEffect\LRC Deck FX.dll'))) 'legacy Deck FX remains'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $VirtualDJTestHome 'Plugins64\Visualisations\LRC Deck.dll'))) 'legacy visualisation remains'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $VirtualDJTestHome 'Plugins64\VideoOverlay\LRC Master.dll'))) 'legacy spaced master filename remains'
    Assert-True ((Get-Content -LiteralPath (Join-Path $VirtualDJTestHome 'settings.xml') -Raw).Contains('<videoAudioOnlyVisualisation>None</videoAudioOnlyVisualisation>')) 'obsolete audio-only selection was not reset'
    Assert-True ((Get-ChildItem -LiteralPath (Join-Path $VirtualDJTestHome 'LRC Lyrics Backups') -Recurse -File).Count -ge 3) 'upgrade backup is incomplete'
    $preInstallBackup = Get-ChildItem -LiteralPath (Join-Path $VirtualDJTestHome 'LRC Lyrics Backups') -Directory | Sort-Object Name | Select-Object -First 1

    & (Join-Path $PackageDirectory 'install-plugin.ps1') -VirtualDJHome $VirtualDJTestHome -PayloadDirectory $PayloadDirectory -NonInteractive -SkipProcessCheck

    & (Join-Path $PackageDirectory 'uninstall-plugin.ps1') -VirtualDJHome $VirtualDJTestHome -NonInteractive -SkipProcessCheck
    foreach ($name in @('LRCMaster.dll', 'LRCBlackOut.dll', 'EmbeddedLyricsTagWriter.py')) {
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $VirtualDJTestHome "Plugins64\VideoOverlay\$name"))) "$name remains after uninstall"
    }
    foreach ($name in @('LRC Master.dll', 'LRC BlackOut.dll')) {
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $VirtualDJTestHome "Plugins64\VideoOverlay\$name"))) "$name legacy filename remains after uninstall"
    }
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $PayloadDirectory 'LRCDeck.dll'))) 'release payload contains LRC Deck'

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
