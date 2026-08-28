param(
    [string]$VirtualDJHome = "",
    [ValidateSet("Full", "Basic")]
    [string]$Edition = "Full"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$EditionDirectory = Join-Path (Join-Path $ProjectRoot "dist") $Edition.ToLowerInvariant()
if ($Edition -eq "Basic") {
    $DllPaths = @(
        (Join-Path $EditionDirectory "EmbeddedLyricsDeckBasic.dll"),
        (Join-Path $EditionDirectory "EmbeddedLyricsMasterBasic.dll")
    )
} else {
    $DllPaths = @(
        (Join-Path $EditionDirectory "EmbeddedLyricsDeck.dll"),
        (Join-Path $EditionDirectory "EmbeddedLyricsMaster.dll"),
        (Join-Path $EditionDirectory "Blackout.dll")
    )
}
if ($DllPaths | Where-Object { -not (Test-Path $_) }) {
    throw "Plugin DLL files were not found. Run build-release.ps1 first."
}

if (-not $VirtualDJHome) {
    $Candidates = @(
        (Join-Path $env:LOCALAPPDATA "VirtualDJ"),
        (Join-Path ([Environment]::GetFolderPath("MyDocuments")) "VirtualDJ")
    ) | Where-Object { Test-Path $_ }
    if ($Candidates.Count -eq 1) {
        $VirtualDJHome = $Candidates[0]
    } elseif ($Candidates.Count -gt 1) {
        throw "Multiple VirtualDJ folders were found. Run with -VirtualDJHome and choose the active folder."
    } else {
        throw "VirtualDJ home folder was not found. Run with -VirtualDJHome."
    }
}

$TargetDirectory = Join-Path $VirtualDJHome "Plugins64\VideoEffect"
New-Item -ItemType Directory -Force -Path $TargetDirectory | Out-Null
foreach ($DllPath in $DllPaths) {
    $Target = Join-Path $TargetDirectory (Split-Path $DllPath -Leaf)
    Copy-Item $DllPath $Target -Force
    Write-Host "Installed: $Target"
}
if ($Edition -eq "Full") {
    $VisualisationsDirectory = Join-Path $VirtualDJHome "Plugins64\Visualisations"
    New-Item -ItemType Directory -Force -Path $VisualisationsDirectory | Out-Null
    $DeckVisualisation = Join-Path $VisualisationsDirectory "EmbeddedLyricsDeck.dll"
    Copy-Item -LiteralPath (Join-Path $EditionDirectory "EmbeddedLyricsDeck.dll") -Destination $DeckVisualisation -Force
    Write-Host "Installed audio-only visualisation: $DeckVisualisation"

    $Writer = Join-Path $ProjectRoot "tools\lyrics_tag_converter.py"
    $WriterTarget = Join-Path $TargetDirectory "EmbeddedLyricsTagWriter.py"
    Copy-Item -LiteralPath $Writer -Destination $WriterTarget -Force
    Write-Host "Installed: $WriterTarget"
}
Write-Host "Restart VirtualDJ. Use Embedded Lyrics Deck on each deck, or Embedded Lyrics Master on master Video FX."
