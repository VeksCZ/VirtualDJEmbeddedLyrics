param([string]$Generator = 'Visual Studio 17 2022')

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Version = (Get-Content -LiteralPath (Join-Path $ProjectRoot 'VERSION') -Raw).Trim()
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid VERSION value: $Version" }

$BuildDirectory = Join-Path $ProjectRoot 'build-release'
$DistRoot = Join-Path $ProjectRoot 'dist'
$PackageName = "LRC-Lyrics-VirtualDJ-v$Version"
$PackageDirectory = Join-Path $DistRoot $PackageName
$PluginsDirectory = Join-Path $PackageDirectory 'Plugins'
$ToolsDirectory = Join-Path $PackageDirectory 'Tools'
$InstallerSource = Join-Path $ProjectRoot 'installer'
$ZipPath = Join-Path $DistRoot "$PackageName.zip"
$ChecksumPath = "$ZipPath.sha256"

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
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
    throw 'Python 3 launcher (py.exe) is required to run the release tests.'
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
py -m unittest discover -s (Join-Path $ProjectRoot 'tests') -p 'test_*.py'
if ($LASTEXITCODE -ne 0) { throw 'Python tests failed.' }

New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
Assert-ChildPath -Child $PackageDirectory -Parent $DistRoot
if (Test-Path -LiteralPath $PackageDirectory) {
    Remove-Item -LiteralPath $PackageDirectory -Recurse -Force
}
foreach ($artifact in @($ZipPath, $ChecksumPath)) {
    Assert-ChildPath -Child $artifact -Parent $DistRoot
    if (Test-Path -LiteralPath $artifact) { Remove-Item -LiteralPath $artifact -Force }
}
New-Item -ItemType Directory -Force -Path $PluginsDirectory, $ToolsDirectory | Out-Null

Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release\LRCMaster.dll') -Destination $PluginsDirectory
Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release\LRCBlackOut.dll') -Destination $PluginsDirectory
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'tools\lyrics_tag_converter.py') -Destination (Join-Path $PluginsDirectory 'EmbeddedLyricsTagWriter.py')

foreach ($name in @(
    'Install.cmd', 'Uninstall.cmd', 'Restore-Backup.cmd',
    'install-plugin.ps1', 'uninstall-plugin.ps1', 'restore-backup.ps1',
    'installer-common.ps1', 'detect-vdj-home.ps1'
)) {
    Copy-Item -LiteralPath (Join-Path $InstallerSource $name) -Destination $PackageDirectory
}
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'VERSION') -Destination $PackageDirectory
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'LyricsTools.cmd') -Destination $PackageDirectory
$toolFiles = @(
    'lyrics_tag_converter.py', 'lrc_tool.py', 'restore_lrc.py',
    'lyrics_tools_gui.py', 'vdj_setup.py', 'vdj_playlist_sync.py'
)
foreach ($name in $toolFiles) {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot "tools\$name") -Destination $ToolsDirectory
}
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'tools\README.md') -Destination (Join-Path $ToolsDirectory 'README.md')
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'requirements.txt') -Destination $ToolsDirectory

$offlineReadme = (Get-Content -LiteralPath (Join-Path $ProjectRoot 'RELEASE-README.txt') -Raw).Replace('{{VERSION}}', $Version)
[System.IO.File]::WriteAllText((Join-Path $PackageDirectory 'README.txt'), $offlineReadme, [System.Text.UTF8Encoding]::new($false))
$releaseNotes = (Get-Content -LiteralPath (Join-Path $ProjectRoot 'RELEASE-NOTES.md') -Raw).Replace('{{VERSION}}', $Version)
[System.IO.File]::WriteAllText((Join-Path $PackageDirectory 'RELEASE-NOTES.md'), $releaseNotes, [System.Text.UTF8Encoding]::new($false))

if (Test-Path -LiteralPath (Join-Path $PluginsDirectory 'LRCDeck.dll')) {
    throw 'The supported release package must not contain LRCDeck.dll.'
}

& (Join-Path $ProjectRoot 'tests\InstallerTests.ps1') -PackageDirectory $PackageDirectory
if ($LASTEXITCODE -ne 0) { throw 'Installer integration tests failed.' }

Compress-Archive -Path (Join-Path $PackageDirectory '*') -DestinationPath $ZipPath -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash
[System.IO.File]::WriteAllText($ChecksumPath, "$hash  $([System.IO.Path]::GetFileName($ZipPath))`r`n", [System.Text.UTF8Encoding]::new($false))

Assert-ChildPath -Child $PackageDirectory -Parent $DistRoot
Remove-Item -LiteralPath $PackageDirectory -Recurse -Force
Assert-ChildPath -Child $BuildDirectory -Parent $ProjectRoot
Remove-Item -LiteralPath $BuildDirectory -Recurse -Force

Write-Host ''
Write-Host "Release ZIP:     $ZipPath" -ForegroundColor Green
Write-Host "SHA-256:         $hash"
