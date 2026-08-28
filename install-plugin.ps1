param([string]$VirtualDJHome = "")

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ReleaseDirectory = Join-Path $ProjectRoot "dist\full"
$DeckDll = Join-Path $ReleaseDirectory "LRC Deck.dll"
$MasterDll = Join-Path $ReleaseDirectory "LRC Master.dll"
$BlackoutDll = Join-Path $ReleaseDirectory "LRC BlackOut.dll"
$Writer = Join-Path $ProjectRoot "tools\lyrics_tag_converter.py"

foreach ($required in @($DeckDll, $MasterDll, $BlackoutDll, $Writer)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required release file was not found: $required. Run build-release.ps1 first."
    }
}

if (-not $VirtualDJHome) {
    $Candidates = @(
        (Join-Path $env:LOCALAPPDATA "VirtualDJ"),
        (Join-Path ([Environment]::GetFolderPath("MyDocuments")) "VirtualDJ")
    ) | Where-Object { Test-Path -LiteralPath $_ }
    if ($Candidates.Count -eq 1) {
        $VirtualDJHome = $Candidates[0]
    } elseif ($Candidates.Count -gt 1) {
        throw "Multiple VirtualDJ folders were found. Run with -VirtualDJHome and choose the active folder."
    } else {
        throw "VirtualDJ home folder was not found. Run with -VirtualDJHome."
    }
}

$PluginsRoot = Join-Path $VirtualDJHome "Plugins64"
$OverlayDirectory = Join-Path $PluginsRoot "VideoOverlay"
$VisualisationsDirectory = Join-Path $PluginsRoot "Visualisations"
$LegacyDirectories = @(
    (Join-Path $PluginsRoot "VideoEffect"),
    $OverlayDirectory,
    $VisualisationsDirectory,
    (Join-Path $PluginsRoot "VideoSource")
)
$LegacyNames = @(
    "EmbeddedLyricsDeck.dll", "EmbeddedLyricsMaster.dll", "Blackout.dll",
    "LRC Deck Basic.dll", "LRC Master Basic.dll", "EmbeddedLyricsTagWriter.py"
)
foreach ($directory in $LegacyDirectories) {
    foreach ($name in $LegacyNames) {
        $path = Join-Path $directory $name
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path
            Write-Host "Removed legacy file: $path"
        }
    }
}
foreach ($misplacedName in @("LRC Deck.dll", "LRC Master.dll", "LRC BlackOut.dll")) {
    $misplaced = Join-Path (Join-Path $PluginsRoot "VideoEffect") $misplacedName
    if (Test-Path -LiteralPath $misplaced) {
        Remove-Item -LiteralPath $misplaced
        Write-Host "Removed misplaced plugin: $misplaced"
    }
}
$oldDeckSource = Join-Path (Join-Path $PluginsRoot "VideoSource") "LRC Deck.dll"
if (Test-Path -LiteralPath $oldDeckSource) {
    Remove-Item -LiteralPath $oldDeckSource
    Write-Host "Removed experimental Deck source: $oldDeckSource"
}

New-Item -ItemType Directory -Force -Path $OverlayDirectory, $VisualisationsDirectory | Out-Null
Copy-Item -LiteralPath $MasterDll -Destination (Join-Path $OverlayDirectory "LRC Master.dll") -Force
Copy-Item -LiteralPath $BlackoutDll -Destination (Join-Path $OverlayDirectory "LRC BlackOut.dll") -Force
Copy-Item -LiteralPath $DeckDll -Destination (Join-Path $VisualisationsDirectory "LRC Deck.dll") -Force
Copy-Item -LiteralPath $Writer -Destination (Join-Path $OverlayDirectory "EmbeddedLyricsTagWriter.py") -Force
Copy-Item -LiteralPath $Writer -Destination (Join-Path $VisualisationsDirectory "EmbeddedLyricsTagWriter.py") -Force

Write-Host "Installed Master and BlackOut into: $OverlayDirectory"
Write-Host "Installed Deck into: $VisualisationsDirectory"
Write-Host "Restart VirtualDJ."
