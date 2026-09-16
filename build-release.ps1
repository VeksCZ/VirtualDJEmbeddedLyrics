param(
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$PythonCommand = 'py',
    [switch]$KeepExtracted,
    # Tags the release commit, pushes the branch and tag, creates (or updates) the GitHub
    # Release with the Windows package, then waits for the macOS GitHub Actions workflow that
    # tag push triggers and attaches its artifacts too. Without this switch the script only
    # produces the local dist/ package, same as before -- v0.8.7 shipped a tag with no GitHub
    # Release at all because that publishing step lived only in a person's memory, not here.
    [switch]$Publish,
    [string]$Repository = 'VeksCZ/VirtualDJEmbeddedLyrics'
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

# RELEASE-NOTES.md accumulates every past version's notes in one file (so historical entries
# stay readable), but a GitHub Release body should only show this version's "## Changes" plus
# the trailing "## Windows download" instructions, not the whole changelog. Pull just those two
# sections out, in the order a person publishing by hand has always assembled them.
function Get-ReleaseNotesBody {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Version)
    $sections = [ordered]@{}
    $current = $null
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^##\s+(.+?)\s*$') {
            $current = $Matches[1]
            if (-not $sections.Contains($current)) { $sections[$current] = [System.Collections.Generic.List[string]]::new() }
        } elseif ($null -ne $current) {
            $sections[$current].Add($line)
        }
    }
    if (-not $sections.Contains('Changes')) { throw "RELEASE-NOTES.md has no '## Changes' section." }
    $downloadKey = $sections.Keys | Where-Object { $_ -like 'Windows download*' } | Select-Object -First 1
    $body = [System.Collections.Generic.List[string]]::new()
    $body.Add("# LRC Lyrics for VirtualDJ $Version"); $body.Add('')
    $body.Add('## Changes'); $body.AddRange($sections['Changes'])
    if ($downloadKey) { $body.Add("## $downloadKey"); $body.AddRange($sections[$downloadKey]) }
    return ($body -join "`n").Trim()
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake was not found. Install Visual Studio 2022 with Desktop development with C++.'
}
if (-not (Get-Command $PythonCommand -ErrorAction SilentlyContinue)) {
    throw "Python 3 is required to run the release tests: $PythonCommand"
}
if ($Publish) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'git is required to -Publish.' }
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'GitHub CLI (gh) is required to -Publish.' }
    gh auth status *> $null
    if ($LASTEXITCODE -ne 0) { throw "gh is not authenticated. Run 'gh auth login' first, then re-run with -Publish." }
    Push-Location $ProjectRoot
    $publishDirty = git status --porcelain
    Pop-Location
    if ($publishDirty) { throw 'Working tree has uncommitted changes; commit before publishing.' }
    $publishTag = "v$Version"
    Push-Location $ProjectRoot
    $publishExistingTag = git tag --list $publishTag
    Pop-Location
    if ($publishExistingTag) { throw "Tag $publishTag already exists locally; bump VERSION or delete the stale tag first." }
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

Copy-Item -LiteralPath (Join-Path $ProjectRoot 'README.md') -Destination (Join-Path $PackageDirectory 'README.txt')
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'README.md') -Destination $PackageDirectory
New-Item -ItemType Directory -Force -Path (Join-Path $PackageDirectory 'tools') | Out-Null
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'tools\README.md') -Destination (Join-Path $PackageDirectory 'tools')

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
if (-not $KeepExtracted) {
    Remove-Item -LiteralPath $PackageDirectory -Recurse -Force
} else {
    Write-Host "Local EXE: $(Join-Path $PackageDirectory 'LyricsTools.exe')" -ForegroundColor Green
}
Assert-ChildPath -Child $BuildDirectory -Parent $ProjectRoot
Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
Write-Host 'Done - release package and checksum are ready.' -ForegroundColor Green

if ($Publish) {
    Write-Host "Publishing $publishTag to $Repository..." -ForegroundColor Cyan
    Push-Location $ProjectRoot
    try {
        $branch = (git rev-parse --abbrev-ref HEAD).Trim()
        git tag -a $publishTag -m "Release $Version"
        if ($LASTEXITCODE -ne 0) { throw 'Failed to create the tag.' }
        git push origin $branch
        if ($LASTEXITCODE -ne 0) { throw 'Failed to push the branch.' }
        git push origin $publishTag
        if ($LASTEXITCODE -ne 0) { throw 'Failed to push the tag.' }

        $notesPath = [System.IO.Path]::GetTempFileName()
        try {
            Get-ReleaseNotesBody -Path (Join-Path $ProjectRoot 'RELEASE-NOTES.md') -Version $Version |
                Set-Content -LiteralPath $notesPath -Encoding utf8NoBOM
            gh release create $publishTag $PackageZip "$PackageZip.sha256" `
                --repo $Repository --title "LRC Lyrics for VirtualDJ $Version" --notes-file $notesPath
            if ($LASTEXITCODE -ne 0) { throw 'Failed to create the GitHub Release.' }
        } finally {
            Remove-Item -LiteralPath $notesPath -Force -ErrorAction SilentlyContinue
        }
        Write-Host "Windows package published: https://github.com/$Repository/releases/tag/$publishTag" -ForegroundColor Green

        # Pushing the tag above already triggered the macOS GitHub Actions workflow (it runs on
        # every push). Find that run and wait for it so the macOS packages can ride along on the
        # same release instead of needing someone to remember a second, separate manual step.
        Write-Host 'Waiting for the macOS build (GitHub Actions) to start...' -ForegroundColor Cyan
        $macRun = $null
        for ($attempt = 0; $attempt -lt 12 -and -not $macRun; $attempt++) {
            Start-Sleep -Seconds 5
            $candidates = gh run list --repo $Repository --workflow=macos-tools.yml --branch $publishTag `
                --json databaseId,status,conclusion --limit 1 | ConvertFrom-Json
            if ($candidates) { $macRun = $candidates[0] }
        }
        if (-not $macRun) {
            Write-Warning "Could not find the macOS workflow run for $publishTag. Attach the macOS packages manually: gh run list --repo $Repository --workflow=macos-tools.yml --branch $publishTag"
        } else {
            gh run watch $macRun.databaseId --repo $Repository --exit-status
            $macFailed = $LASTEXITCODE -ne 0
            if ($macFailed) {
                Write-Warning "The macOS build failed; the release is published Windows-only. Inspect it with: gh run view $($macRun.databaseId) --repo $Repository --log-failed"
            } else {
                $macDir = Join-Path ([System.IO.Path]::GetTempPath()) "lrc-macos-$publishTag"
                if (Test-Path -LiteralPath $macDir) { Remove-Item -LiteralPath $macDir -Recurse -Force }
                gh run download $macRun.databaseId --repo $Repository --dir $macDir
                if ($LASTEXITCODE -ne 0) {
                    Write-Warning "Could not download the macOS artifacts for run $($macRun.databaseId); attach them manually."
                } else {
                    $macAssets = Get-ChildItem -LiteralPath $macDir -Recurse -File
                    if ($macAssets) {
                        gh release upload $publishTag @($macAssets.FullName) --repo $Repository
                        if ($LASTEXITCODE -ne 0) { Write-Warning 'Failed to upload the macOS assets to the release.' }
                        else { Write-Host 'macOS packages attached.' -ForegroundColor Green }
                    }
                }
                Remove-Item -LiteralPath $macDir -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
        Write-Host "Release ready: https://github.com/$Repository/releases/tag/$publishTag" -ForegroundColor Green
    } finally {
        Pop-Location
    }
}
