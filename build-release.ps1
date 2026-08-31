param(
    [string]$Generator = "Visual Studio 17 2022",
    [switch]$BuildLrcDeck
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDirectory = Join-Path $ProjectRoot "build-release"
$FullDirectory = Join-Path $ProjectRoot "dist\full"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake was not found. Install Visual Studio 2022 with Desktop development with C++."
}

$DeckBuildSetting = if ($BuildLrcDeck) { "ON" } else { "OFF" }
cmake -S $ProjectRoot -B $BuildDirectory -G $Generator -A x64 -DBUILD_TESTING=ON "-DBUILD_LRC_DECK=$DeckBuildSetting"
cmake --build $BuildDirectory --config Release
ctest --test-dir $BuildDirectory -C Release --output-on-failure

if (Get-Command py -ErrorAction SilentlyContinue) {
    py -m unittest discover -s (Join-Path $ProjectRoot "tests") -p "test_*.py"
    if ($LASTEXITCODE -ne 0) { throw "Python tests failed." }
}

New-Item -ItemType Directory -Force -Path $FullDirectory | Out-Null
Get-ChildItem -LiteralPath $FullDirectory -File -ErrorAction SilentlyContinue | Remove-Item
if ($BuildLrcDeck) {
    Copy-Item (Join-Path $BuildDirectory "Release\LRC Deck.dll") $FullDirectory
}
Copy-Item (Join-Path $BuildDirectory "Release\LRC Master.dll") $FullDirectory
Copy-Item (Join-Path $BuildDirectory "Release\LRC BlackOut.dll") $FullDirectory
Copy-Item (Join-Path $ProjectRoot "tools\lyrics_tag_converter.py") (Join-Path $FullDirectory "EmbeddedLyricsTagWriter.py")
Copy-Item (Join-Path $ProjectRoot "RELEASE-README.txt") (Join-Path $FullDirectory "README.txt")
Write-Host "Release files created in: $FullDirectory"
