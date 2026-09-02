param([string]$ExplicitPath = '')

$ErrorActionPreference = 'Stop'
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $OutputEncoding
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptRoot 'installer-common.ps1')

$candidates = @(Get-VirtualDJCandidates)
$selected = $null
$valid = $false
$message = ''

if ($ExplicitPath) {
    try {
        $selected = ConvertTo-FullPath $ExplicitPath
        $valid = Test-VirtualDJHome $selected
        if (-not $valid) {
            $message = 'The selected folder is not a VirtualDJ home folder.'
        }
    } catch {
        $message = $_.Exception.Message
    }
} else {
    $preferred = @($candidates | Where-Object Preferred)
    if ($preferred.Count -eq 1) {
        $selected = $preferred[0].Path
        $valid = $true
    } elseif ($candidates.Count -eq 1) {
        $selected = $candidates[0].Path
        $valid = $true
    } elseif ($candidates.Count -gt 1) {
        $message = 'Multiple VirtualDJ home folders were found. Select the active folder.'
    } else {
        $message = 'No VirtualDJ home folder was found automatically.'
    }
}

[ordered]@{
    Valid = $valid
    Selected = $selected
    Message = $message
    VirtualDJRunning = [bool](Get-Process -Name 'virtualdj' -ErrorAction SilentlyContinue)
    Candidates = @($candidates | ForEach-Object {
        [ordered]@{
            Path = $_.Path
            Source = $_.Source
            Preferred = [bool]$_.Preferred
        }
    })
} | ConvertTo-Json -Depth 4 -Compress
