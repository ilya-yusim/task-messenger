<#
.SYNOPSIS
  Stop a tm-worker run that was started in a GitHub Codespace by
  start_workers_codespace.ps1.

.PARAMETER Codespace
  Codespace name. If omitted, uses the first running codespace.

.PARAMETER Run
  Run ID (e.g. 20260427-153012) or "latest". Defaults to "latest".

.PARAMETER GraceSeconds
  Grace seconds before the remote stop script escalates to SIGKILL. Default 5.

.EXAMPLE
  .\stop_workers_codespace.ps1 -Codespace fluffy-disco-abc123
#>
[CmdletBinding(PositionalBinding=$false)]
param(
    [string]$Codespace,
    [string]$Run = "latest",
    [int]$GraceSeconds = 5
)

$ErrorActionPreference = 'Stop'

$gh = (Get-Command gh -ErrorAction SilentlyContinue)
if (-not $gh) { throw "gh CLI not found in PATH." }

if (-not $Codespace) {
    $listOutput = & gh codespace list --json name,state 2>&1
    $listJson = ($listOutput | Where-Object { $_ -is [string] -and $_.StartsWith('[') }) -join "`n"
    if (-not $listJson.Trim()) {
        $msg = ($listOutput | Out-String).Trim()
        if ($msg -match '403' -or $msg -match 'admin rights') {
            throw "gh can't list codespaces: $msg`nYour gh auth is missing the 'codespace' scope or is logged into the wrong account. Try:`n  gh auth refresh -h github.com -s codespace`nOr re-login: gh auth login --scopes codespace"
        }
        throw "Failed to list codespaces: $msg"
    }
    $list = $listJson | ConvertFrom-Json
    if (-not $list) { throw "No codespaces found." }
    $running = $list | Where-Object { $_.state -in @('Available','Running') }
    if ($running) {
        $Codespace = $running[0].name
    } else {
        $Codespace = $list[0].name
        Write-Warning "No running codespace; will resume '$Codespace' (state: $($list[0].state)) just to stop workers."
    }
    Write-Host "Using codespace: $Codespace"
}

# If "latest", first try the local pointer for this codespace; fall back to remote.
if ($Run -eq 'latest') {
    $localLatest = Join-Path $env:LOCALAPPDATA "tm-worker-farm\runs\codespace-$Codespace\latest.txt"
    if (Test-Path -LiteralPath $localLatest) {
        $Run = (Get-Content -LiteralPath $localLatest -Raw).Trim()
    } else {
        $Run = (& gh codespace ssh -c $Codespace -- 'cat $HOME/.cache/tm-worker-farm/runs/latest.txt').Trim()
        if ($LASTEXITCODE -or -not $Run) { throw "Could not resolve latest run on $Codespace" }
    }
}

$remoteDir = '~/.local/share/tm-worker-farm'
$remoteCmd = "$remoteDir/stop_workers_local.sh -r '$Run' -g $GraceSeconds"

Write-Host "Stopping run $Run on $Codespace ..."
& gh codespace ssh -c $Codespace -- $remoteCmd
if ($LASTEXITCODE) { throw "Remote stop_workers_local.sh failed (exit $LASTEXITCODE)" }

Write-Host ""
Write-Host "Codespace $Codespace : run $Run stopped."
