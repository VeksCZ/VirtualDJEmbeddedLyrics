param([string]$VirtualDJHome = "")

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ReleaseDirectory = Join-Path $ProjectRoot "dist\full"
$DeckDll = Join-Path $ReleaseDirectory "LRC Deck.dll"
$MasterDll = Join-Path $ReleaseDirectory "LRC Master.dll"
$BlackoutDll = Join-Path $ReleaseDirectory "LRC BlackOut.dll"
$Writer = Join-Path $ProjectRoot "tools\lyrics_tag_converter.py"

foreach ($required in @($MasterDll, $BlackoutDll, $Writer)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required release file was not found: $required. Run build-release.ps1 first."
    }
}
$InstallDeck = Test-Path -LiteralPath $DeckDll

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
$VideoFxDirectory = Join-Path $PluginsRoot "VideoEffect"
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

# Remove saved state left by older builds that were incorrectly installed as
# ordinary Video Effects. Keeping these files can leave duplicate legacy names
# visible after the DLL itself has been removed.
$LegacyVideoFxStateNames = @(
    "EmbeddedLyricsDeck.ini", "EmbeddedLyricsMaster.ini",
    "LRC Deck.ini", "LRC Deck_2.ini", "LRC Deck FX.ini", "LRC Master.ini"
)
foreach ($name in $LegacyVideoFxStateNames) {
    $path = Join-Path $VideoFxDirectory $name
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path
        Write-Host "Removed legacy Video FX state: $path"
    }
}

$obsoleteDeckFx = Join-Path $VideoFxDirectory "LRC Deck FX.dll"
if (Test-Path -LiteralPath $obsoleteDeckFx) {
    Remove-Item -LiteralPath $obsoleteDeckFx
    Write-Host "Removed obsolete Deck FX plugin: $obsoleteDeckFx"
}

if (-not $InstallDeck) {
    foreach ($name in @("LRC Deck.dll", "LRC Deck.ini", "LRC Deck_2.ini")) {
        $path = Join-Path $VisualisationsDirectory $name
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path
            Write-Host "Removed disabled audio-only plugin state: $path"
        }
    }
    $settingsPath = Join-Path $VirtualDJHome "settings.xml"
    if (Test-Path -LiteralPath $settingsPath) {
        $settingsText = [System.IO.File]::ReadAllText($settingsPath)
        $selectedDeck = "<videoAudioOnlyVisualisation>LRC Deck</videoAudioOnlyVisualisation>"
        $disabledDeck = "<videoAudioOnlyVisualisation>None</videoAudioOnlyVisualisation>"
        if ($settingsText.Contains($selectedDeck)) {
            $settingsText = $settingsText.Replace($selectedDeck, $disabledDeck)
            [System.IO.File]::WriteAllText(
                $settingsPath,
                $settingsText,
                [System.Text.UTF8Encoding]::new($false)
            )
            Write-Host "Reset the disabled audio-only source to None in: $settingsPath"
        }
    }
}

New-Item -ItemType Directory -Force -Path $OverlayDirectory | Out-Null
Copy-Item -LiteralPath $MasterDll -Destination (Join-Path $OverlayDirectory "LRC Master.dll") -Force
Copy-Item -LiteralPath $BlackoutDll -Destination (Join-Path $OverlayDirectory "LRC BlackOut.dll") -Force
Copy-Item -LiteralPath $Writer -Destination (Join-Path $OverlayDirectory "EmbeddedLyricsTagWriter.py") -Force

Write-Host "Installed Master and BlackOut into: $OverlayDirectory"
if ($InstallDeck) {
    New-Item -ItemType Directory -Force -Path $VisualisationsDirectory | Out-Null
    Copy-Item -LiteralPath $DeckDll -Destination (Join-Path $VisualisationsDirectory "LRC Deck.dll") -Force
    Copy-Item -LiteralPath $Writer -Destination (Join-Path $VisualisationsDirectory "EmbeddedLyricsTagWriter.py") -Force
    Write-Host "Installed optional Deck audio-only source into: $VisualisationsDirectory"
} else {
    Write-Host "LRC Deck is disabled in this build."
}
Write-Host "Restart VirtualDJ."
