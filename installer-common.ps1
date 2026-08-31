function ConvertTo-FullPath {
    param([Parameter(Mandatory)][string]$Path)
    $expanded = [Environment]::ExpandEnvironmentVariables($Path.Trim().Trim('"'))
    return [System.IO.Path]::GetFullPath($expanded)
}

function Test-VirtualDJHome {
    param([Parameter(Mandatory)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return $false }
    foreach ($marker in @('settings.xml', 'database.xml', 'Plugins64', 'Folders')) {
        if (Test-Path -LiteralPath (Join-Path $Path $marker)) { return $true }
    }
    return $false
}

function Get-VirtualDJCandidates {
    $rawCandidates = [System.Collections.Generic.List[object]]::new()
    try {
        $registryHome = Get-ItemPropertyValue -Path 'HKCU:\Software\VirtualDJ' -Name 'HomeFolder' -ErrorAction Stop
        if ($registryHome) {
            $rawCandidates.Add([pscustomobject]@{ Path = [string]$registryHome; Source = 'VirtualDJ registry HomeFolder'; Preferred = $true })
        }
    } catch {}

    if ($env:LOCALAPPDATA) {
        $rawCandidates.Add([pscustomobject]@{
            Path = (Join-Path $env:LOCALAPPDATA 'VirtualDJ')
            Source = 'current Windows default'
            Preferred = $false
        })
    }
    $documents = [Environment]::GetFolderPath('MyDocuments')
    if ($documents) {
        $rawCandidates.Add([pscustomobject]@{
            Path = (Join-Path $documents 'VirtualDJ')
            Source = 'legacy Documents location'
            Preferred = $false
        })
    }

    $seen = @{}
    foreach ($candidate in $rawCandidates) {
        try { $fullPath = ConvertTo-FullPath $candidate.Path } catch { continue }
        $key = $fullPath.TrimEnd('\').ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        if (Test-VirtualDJHome $fullPath) {
            [pscustomobject]@{
                Path = $fullPath
                Source = $candidate.Source
                Preferred = $candidate.Preferred
            }
        }
    }
}

function Resolve-VirtualDJHome {
    param(
        [string]$ExplicitPath,
        [switch]$NonInteractive
    )

    if ($ExplicitPath) {
        $fullPath = ConvertTo-FullPath $ExplicitPath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Container)) {
            throw "The selected VirtualDJ home folder does not exist: $fullPath"
        }
        return $fullPath
    }

    $candidates = @(Get-VirtualDJCandidates)
    $preferred = @($candidates | Where-Object Preferred)
    if ($preferred.Count -eq 1) { return $preferred[0].Path }
    if ($candidates.Count -eq 1) { return $candidates[0].Path }

    if ($NonInteractive) {
        $found = if ($candidates.Count) { ($candidates.Path -join '; ') } else { 'none' }
        throw "VirtualDJ home folder could not be selected automatically. Candidates: $found. Pass -VirtualDJHome."
    }

    Write-Host ''
    Write-Host 'VirtualDJ home folder selection' -ForegroundColor Cyan
    Write-Host 'In VirtualDJ, Settings > Options > the small cog button opens the active home folder.'
    if ($candidates.Count -gt 0) {
        for ($index = 0; $index -lt $candidates.Count; ++$index) {
            Write-Host ("  [{0}] {1} ({2})" -f ($index + 1), $candidates[$index].Path, $candidates[$index].Source)
        }
        Write-Host '  [M] Enter another folder manually'
        while ($true) {
            $choice = (Read-Host 'Choose the active VirtualDJ folder').Trim()
            $number = 0
            if ([int]::TryParse($choice, [ref]$number) -and $number -ge 1 -and $number -le $candidates.Count) {
                return $candidates[$number - 1].Path
            }
            if ($choice -match '^[mM]$') { break }
            Write-Warning 'Enter one of the displayed numbers or M.'
        }
    }

    while ($true) {
        $manual = Read-Host 'Paste the active VirtualDJ home folder path'
        try { $fullPath = ConvertTo-FullPath $manual } catch { Write-Warning $_.Exception.Message; continue }
        if (Test-Path -LiteralPath $fullPath -PathType Container) { return $fullPath }
        Write-Warning "Folder not found: $fullPath"
    }
}

function Assert-VirtualDJClosed {
    param([switch]$SkipProcessCheck)
    if ($SkipProcessCheck) { return }
    if (Get-Process -Name 'virtualdj' -ErrorAction SilentlyContinue) {
        throw 'VirtualDJ is running. Close VirtualDJ completely, then run this command again.'
    }
}

function Resolve-LrcPayloadDirectory {
    param(
        [Parameter(Mandatory)][string]$ScriptRoot,
        [string]$ExplicitPath
    )
    if ($ExplicitPath) {
        $explicitFullPath = ConvertTo-FullPath $ExplicitPath
        if ((Test-Path -LiteralPath (Join-Path $explicitFullPath 'LRC Master.dll') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $explicitFullPath 'LRC BlackOut.dll') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $explicitFullPath 'EmbeddedLyricsTagWriter.py') -PathType Leaf)) {
            return $explicitFullPath
        }
        throw "The selected plugin payload is incomplete: $explicitFullPath"
    }

    $candidates = [System.Collections.Generic.List[string]]::new()
    $candidates.Add((Join-Path $ScriptRoot 'Plugins'))

    $versionFile = Join-Path $ScriptRoot 'VERSION'
    if (Test-Path -LiteralPath $versionFile) {
        $version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
        if ($version) {
            $candidates.Add((Join-Path $ScriptRoot "dist\LRC-Lyrics-VirtualDJ-v$version\Plugins"))
        }
    }
    $candidates.Add((Join-Path $ScriptRoot 'dist\full'))

    foreach ($candidate in $candidates) {
        if ((Test-Path -LiteralPath (Join-Path $candidate 'LRC Master.dll') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $candidate 'LRC BlackOut.dll') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $candidate 'EmbeddedLyricsTagWriter.py') -PathType Leaf)) {
            return (ConvertTo-FullPath $candidate)
        }
    }
    throw 'Plugin payload was not found. Download and extract the complete release ZIP, or run build-release.ps1 first.'
}

function Copy-LrcBackupItem {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$VirtualDJHome,
        [Parameter(Mandatory)][string]$BackupRoot
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $homePrefix = (ConvertTo-FullPath $VirtualDJHome).TrimEnd('\') + '\'
    $fullPath = ConvertTo-FullPath $Path
    if (-not $fullPath.StartsWith($homePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to back up a file outside VirtualDJ home: $fullPath"
    }
    $relative = $fullPath.Substring($homePrefix.Length)
    $destination = Join-Path $BackupRoot $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $fullPath -Destination $destination -Force
}

function Copy-LrcVerifiedFile {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination
    )
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
    $sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    $destinationHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    if ($sourceHash -ne $destinationHash) {
        throw "Installed file verification failed: $Destination"
    }
}
