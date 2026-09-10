param(
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$PythonCommand = 'py'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Version = (Get-Content -LiteralPath (Join-Path $ProjectRoot 'VERSION') -Raw).Trim()
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid VERSION value: $Version" }

$BuildDirectory = Join-Path $ProjectRoot 'build-release'
$DistRoot = Join-Path $ProjectRoot 'dist'
$PackageName = "LyricsTools-Windows-v$Version"
$PackageDirectory = Join-Path $DistRoot $PackageName
$InternalDirectory = Join-Path $PackageDirectory '_internal'
$PluginsDirectory = Join-Path $InternalDirectory 'Plugins'
$PackageZip = Join-Path $DistRoot "$PackageName.zip"

function Assert-ChildPath {
    param([Parameter(Mandatory)][string]$Child, [Parameter(Mandatory)][string]$Parent)
    $childFull = [System.IO.Path]::GetFullPath($Child)
    $parentFull = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $childFull.StartsWith($parentFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the expected directory: $childFull"
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake was not found. Install Visual Studio 2022 with Desktop development with C++.'
}
if (-not (Get-Command $PythonCommand -ErrorAction SilentlyContinue)) {
    throw "Python 3 is required to run the release tests: $PythonCommand"
}
& $PythonCommand -c "import PyInstaller" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw 'PyInstaller is required for releases. Use the prepared .venv-build environment.'
}

Assert-ChildPath -Child $BuildDirectory -Parent $ProjectRoot
if (Test-Path -LiteralPath $BuildDirectory) {
    Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
}
cmake -S $ProjectRoot -B $BuildDirectory -G $Generator -A x64 -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build $BuildDirectory --config Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
ctest --test-dir $BuildDirectory -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'C++ tests failed.' }
& $PythonCommand -m unittest discover -s (Join-Path $ProjectRoot 'tests') -p 'test_*.py'
if ($LASTEXITCODE -ne 0) { throw 'Python tests failed.' }

New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
Assert-ChildPath -Child $PackageDirectory -Parent $DistRoot
if (Test-Path -LiteralPath $PackageDirectory) {
    Remove-Item -LiteralPath $PackageDirectory -Recurse -Force
}
foreach ($artifact in @($PackageZip, "$PackageZip.sha256")) {
    Assert-ChildPath -Child $artifact -Parent $DistRoot
    if (Test-Path -LiteralPath $artifact) { Remove-Item -LiteralPath $artifact -Force }
}
New-Item -ItemType Directory -Force -Path $PackageDirectory, $PluginsDirectory | Out-Null

$AppDist = Join-Path $BuildDirectory 'app-dist'
& $PythonCommand -m PyInstaller --noconfirm --clean --onefile --windowed --name LyricsTools `
    --paths (Join-Path $ProjectRoot 'tools') --distpath $AppDist `
    --workpath (Join-Path $BuildDirectory 'app-build') --specpath (Join-Path $BuildDirectory 'app-spec') `
    (Join-Path $ProjectRoot 'tools\lyrics_tools_gui.py')
if ($LASTEXITCODE -ne 0) { throw 'Standalone LyricsTools build failed.' }
Copy-Item -LiteralPath (Join-Path $AppDist 'LyricsTools.exe') -Destination $PackageDirectory

Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release\LRCMaster.dll') -Destination $PluginsDirectory
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release\LRCBlackOut.dll') -Destination $PluginsDirectory
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'tools\lyrics_tag_converter.py') -Destination (Join-Path $PluginsDirectory 'EmbeddedLyricsTagWriter.py')
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'VERSION') -Destination $InternalDirectory

$readme = @"
LyricsTools $Version

Open LyricsTools.exe. No Python installation is required.
The first tab installs or updates LRC Master and LRC BlackOut for VirtualDJ.
All support files are kept in _internal; users normally do not need to open it.
"@
[System.IO.File]::WriteAllText((Join-Path $PackageDirectory 'README.txt'), $readme, [System.Text.UTF8Encoding]::new($false))

if (-not (Test-Path -LiteralPath (Join-Path $PackageDirectory 'LyricsTools.exe'))) {
    throw 'LyricsTools.exe is missing from the package root.'
}
if (Test-Path -LiteralPath (Join-Path $PackageDirectory 'LRCPluginSetup.exe')) {
    throw 'The obsolete standalone plugin setup must not be packaged.'
}

Compress-Archive -Path $PackageDirectory -DestinationPath $PackageZip -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $PackageZip -Algorithm SHA256).Hash
[System.IO.File]::WriteAllText("$PackageZip.sha256", "$hash  $([System.IO.Path]::GetFileName($PackageZip))`r`n", [System.Text.UTF8Encoding]::new($false))
Write-Host "Release ZIP: $PackageZip" -ForegroundColor Green
Write-Host "SHA-256:     $hash"

Assert-ChildPath -Child $PackageDirectory -Parent $DistRoot
Remove-Item -LiteralPath $PackageDirectory -Recurse -Force
Assert-ChildPath -Child $BuildDirectory -Parent $ProjectRoot
Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
