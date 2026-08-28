param(
    [string]$Generator = "Visual Studio 17 2022"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDirectory = Join-Path $ProjectRoot "build-release"
$DistDirectory = Join-Path $ProjectRoot "dist"
$FullDirectory = Join-Path $DistDirectory "full"
$BasicDirectory = Join-Path $DistDirectory "basic"

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake was not found. Install Visual Studio 2022 with Desktop development with C++ and CMake tools."
}

cmake -S $ProjectRoot -B $BuildDirectory -G $Generator -A x64 -DBUILD_TESTING=ON
cmake --build $BuildDirectory --config Release
ctest --test-dir $BuildDirectory -C Release --output-on-failure

New-Item -ItemType Directory -Force -Path $DistDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $FullDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $BasicDirectory | Out-Null
Copy-Item (Join-Path $BuildDirectory "Release\EmbeddedLyricsDeck.dll") $FullDirectory -Force
Copy-Item (Join-Path $BuildDirectory "Release\EmbeddedLyricsMaster.dll") $FullDirectory -Force
Copy-Item (Join-Path $BuildDirectory "Release\Blackout.dll") $FullDirectory -Force
Copy-Item (Join-Path $BuildDirectory "Release\EmbeddedLyricsDeckBasic.dll") $BasicDirectory -Force
Copy-Item (Join-Path $BuildDirectory "Release\EmbeddedLyricsMasterBasic.dll") $BasicDirectory -Force
Write-Host "Release created:"
Write-Host "  Full:  $FullDirectory"
Write-Host "  Basic: $BasicDirectory"
