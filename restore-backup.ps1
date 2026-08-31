param(
    [string]$VirtualDJHome = '',
    [string]$BackupDirectory = '',
    [switch]$NonInteractive,
    [switch]$SkipProcessCheck
)

$ErrorActionPreference = 'Stop'
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptRoot 'installer-common.ps1')

Assert-VirtualDJClosed -SkipProcessCheck:$SkipProcessCheck
$VirtualDJHome = Resolve-VirtualDJHome -ExplicitPath $VirtualDJHome -NonInteractive:$NonInteractive
$BackupsRoot = Join-Path $VirtualDJHome 'LRC Lyrics Backups'
if (-not (Test-Path -LiteralPath $BackupsRoot -PathType Container)) {
    throw "No LRC Lyrics backups were found in: $BackupsRoot"
}

$available = @(Get-ChildItem -LiteralPath $BackupsRoot -Directory | Sort-Object Name -Descending)
if ($BackupDirectory) {
    $selected = ConvertTo-FullPath $BackupDirectory
} elseif ($NonInteractive) {
    if (-not $available.Count) { throw "No LRC Lyrics backups were found in: $BackupsRoot" }
    $selected = $available[0].FullName
} else {
    if (-not $available.Count) { throw "No LRC Lyrics backups were found in: $BackupsRoot" }
    Write-Host ''
    Write-Host 'Available LRC Lyrics backups' -ForegroundColor Cyan
    for ($index = 0; $index -lt $available.Count; ++$index) {
        Write-Host ("  [{0}] {1}" -f ($index + 1), $available[$index].Name)
    }
    while ($true) {
        $choice = (Read-Host 'Choose a backup to restore').Trim()
        $number = 0
        if ([int]::TryParse($choice, [ref]$number) -and $number -ge 1 -and $number -le $available.Count) {
            $selected = $available[$number - 1].FullName
            break
        }
        Write-Warning 'Enter one of the displayed numbers.'
    }
}

$backupsPrefix = (ConvertTo-FullPath $BackupsRoot).TrimEnd('\') + '\'
$selected = ConvertTo-FullPath $selected
if (-not $selected.StartsWith($backupsPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $selected -PathType Container)) {
    throw "Refusing to restore a folder outside the LRC Lyrics backup directory: $selected"
}

$files = @(Get-ChildItem -LiteralPath $selected -Recurse -File)
if (-not $files.Count) { throw "The selected backup is empty: $selected" }
foreach ($file in $files) {
    $relative = $file.FullName.Substring($selected.TrimEnd('\').Length).TrimStart('\')
    $destination = Join-Path $VirtualDJHome $relative
    Copy-LrcVerifiedFile -Source $file.FullName -Destination $destination
    Write-Host "Restored: $destination"
}

Write-Host ''
Write-Host 'The selected LRC Lyrics backup was restored successfully.' -ForegroundColor Green
Write-Host "Backup: $selected"
Write-Host 'Start VirtualDJ again.'
