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
$SetupPackageName = "LRC-Plugin-Setup-Windows-v$Version"
$LyricsPackageName = "LyricsTools-Windows-v$Version"
$SetupPackageDirectory = Join-Path $DistRoot $SetupPackageName
$LyricsPackageDirectory = Join-Path $DistRoot $LyricsPackageName
$PluginsDirectory = Join-Path $SetupPackageDirectory 'Plugins'
$ToolsDirectory = Join-Path $SetupPackageDirectory 'Tools'
$InstallerSource = Join-Path $ProjectRoot 'installer'
$SetupZipPath = Join-Path $DistRoot "$SetupPackageName.zip"
$LyricsZipPath = Join-Path $DistRoot "$LyricsPackageName.zip"

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
    throw 'PyInstaller is required for releases. Run: py -m pip install -r requirements-build.txt'
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
foreach ($directory in @($SetupPackageDirectory, $LyricsPackageDirectory)) {
    Assert-ChildPath -Child $directory -Parent $DistRoot
    if (Test-Path -LiteralPath $directory) { Remove-Item -LiteralPath $directory -Recurse -Force }
}
foreach ($artifact in @($SetupZipPath, "$SetupZipPath.sha256", $LyricsZipPath, "$LyricsZipPath.sha256")) {
    Assert-ChildPath -Child $artifact -Parent $DistRoot
    if (Test-Path -LiteralPath $artifact) { Remove-Item -LiteralPath $artifact -Force }
}
New-Item -ItemType Directory -Force -Path $PluginsDirectory, $ToolsDirectory, $LyricsPackageDirectory | Out-Null

$SetupBuild = Join-Path $BuildDirectory 'plugin-setup-build'
$SetupDist = Join-Path $BuildDirectory 'plugin-setup-dist'
$SetupSpec = Join-Path $BuildDirectory 'plugin-setup-spec'
& $PythonCommand -m PyInstaller --noconfirm --clean --onefile --windowed --name LRCPluginSetup `
    --paths (Join-Path $ProjectRoot 'tools') --distpath $SetupDist --workpath $SetupBuild `
    --specpath $SetupSpec (Join-Path $ProjectRoot 'tools\plugin_setup_gui.py')
if ($LASTEXITCODE -ne 0) { throw 'Standalone plugin setup build failed.' }
Copy-Item -LiteralPath (Join-Path $SetupDist 'LRCPluginSetup.exe') -Destination $SetupPackageDirectory
& $PythonCommand -m PyInstaller --noconfirm --clean --onefile --windowed --name LyricsTools `
    --paths (Join-Path $ProjectRoot 'tools') --distpath $SetupDist `
    --workpath (Join-Path $BuildDirectory 'lyrics-tools-build') --specpath $SetupSpec `
    (Join-Path $ProjectRoot 'tools\lyrics_tools_gui.py')
if ($LASTEXITCODE -ne 0) { throw 'Standalone LyricsTools build failed.' }
Copy-Item -LiteralPath (Join-Path $SetupDist 'LyricsTools.exe') -Destination $LyricsPackageDirectory

Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release\LRCMaster.dll') -Destination $PluginsDirectory
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release\LRCBlackOut.dll') -Destination $PluginsDirectory
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'tools\lyrics_tag_converter.py') -Destination (Join-Path $PluginsDirectory 'EmbeddedLyricsTagWriter.py')

foreach ($name in @(
    'Install.cmd', 'Uninstall.cmd', 'Restore-Backup.cmd',
    'install-plugin.ps1', 'uninstall-plugin.ps1', 'restore-backup.ps1',
    'installer-common.ps1', 'detect-vdj-home.ps1'
)) {
    Copy-Item -LiteralPath (Join-Path $InstallerSource $name) -Destination $SetupPackageDirectory
}
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'VERSION') -Destination $SetupPackageDirectory
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'VERSION') -Destination $LyricsPackageDirectory
$toolFiles = @(
    'plugin_setup_gui.py', 'gui_common.py', 'vdj_setup.py'
)
foreach ($name in $toolFiles) {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot "tools\$name") -Destination $ToolsDirectory
}
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'tools\README.md') -Destination (Join-Path $ToolsDirectory 'README.md')
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'requirements.txt') -Destination $ToolsDirectory

$offlineReadme = (Get-Content -LiteralPath (Join-Path $ProjectRoot 'RELEASE-README.txt') -Raw).Replace('{{VERSION}}', $Version)
[System.IO.File]::WriteAllText((Join-Path $SetupPackageDirectory 'README.txt'), $offlineReadme, [System.Text.UTF8Encoding]::new($false))
$lyricsReadme = @"
LyricsTools $Version

Open LyricsTools.exe. This standalone application does not require Python.
It manages VirtualDJ playlists, local LRC/TXT files, embedded lyrics, online
lyrics sources, backups, and the local problem queue. It does not install the
VirtualDJ plugin; download the separate LRC Plugin Setup package for that.
"@
[System.IO.File]::WriteAllText((Join-Path $LyricsPackageDirectory 'README.txt'), $lyricsReadme, [System.Text.UTF8Encoding]::new($false))
$releaseNotes = (Get-Content -LiteralPath (Join-Path $ProjectRoot 'RELEASE-NOTES.md') -Raw).Replace('{{VERSION}}', $Version)
[System.IO.File]::WriteAllText((Join-Path $SetupPackageDirectory 'RELEASE-NOTES.md'), $releaseNotes, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText((Join-Path $LyricsPackageDirectory 'RELEASE-NOTES.md'), $releaseNotes, [System.Text.UTF8Encoding]::new($false))

if (Test-Path -LiteralPath (Join-Path $PluginsDirectory 'LRCDeck.dll')) {
    throw 'The supported release package must not contain LRCDeck.dll.'
}

& (Join-Path $ProjectRoot 'tests\InstallerTests.ps1') -PackageDirectory $SetupPackageDirectory
if ($LASTEXITCODE -ne 0) { throw 'Installer integration tests failed.' }
if (Test-Path -LiteralPath (Join-Path $SetupPackageDirectory 'LyricsTools.exe')) { throw 'Setup package contains LyricsTools.' }
if (Test-Path -LiteralPath (Join-Path $LyricsPackageDirectory 'LRCPluginSetup.exe')) { throw 'LyricsTools package contains Plugin Setup.' }

foreach ($item in @(
    @{ Directory = $SetupPackageDirectory; Zip = $SetupZipPath },
    @{ Directory = $LyricsPackageDirectory; Zip = $LyricsZipPath }
)) {
    Compress-Archive -Path $item.Directory -DestinationPath $item.Zip -CompressionLevel Optimal
    $hash = (Get-FileHash -LiteralPath $item.Zip -Algorithm SHA256).Hash
    [System.IO.File]::WriteAllText("$($item.Zip).sha256", "$hash  $([System.IO.Path]::GetFileName($item.Zip))`r`n", [System.Text.UTF8Encoding]::new($false))
    Write-Host "Release ZIP: $($item.Zip)" -ForegroundColor Green
    Write-Host "SHA-256:     $hash"
}

foreach ($directory in @($SetupPackageDirectory, $LyricsPackageDirectory)) {
    Assert-ChildPath -Child $directory -Parent $DistRoot
    Remove-Item -LiteralPath $directory -Recurse -Force
}
Assert-ChildPath -Child $BuildDirectory -Parent $ProjectRoot
Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
