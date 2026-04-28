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

# If "latest", first try the local pointer for this codespace. If we have
# it locally, great — saves a round trip. If not, the remote bash below
# resolves it on the codespace side as part of a single ssh.
if ($Run -eq 'latest') {
    $localLatest = Join-Path $env:LOCALAPPDATA "tm-worker-farm\runs\codespace-$Codespace\latest.txt"
    if (Test-Path -LiteralPath $localLatest) {
        $Run = (Get-Content -LiteralPath $localLatest -Raw).Trim()
    }
}

# One ssh round trip: resolve `latest` if still unresolved, then run the
# stop helper. Piping bash over stdin lets us keep both steps in a single
# session without paying the gh codespace ssh setup cost twice.
Write-Host "Stopping run $Run on $Codespace ..."
$remoteScript = @'
set -e
REMOTE_DIR="$HOME/.local/share/tm-worker-farm"
run="__RUN__"
if [ "$run" = "latest" ]; then
    run=$(cat "$HOME/.cache/tm-worker-farm/runs/latest.txt" 2>/dev/null || true)
    if [ -z "$run" ]; then
        echo "Could not resolve latest run on remote" >&2
        exit 1
    fi
fi
printf '__TM_RUN=%s\n' "$run"
"$REMOTE_DIR/stop_workers_local.sh" -r "$run" -g __GRACE__
'@
$remoteScript = $remoteScript `
    -replace '__RUN__',   $Run `
    -replace '__GRACE__', [string]$GraceSeconds
# bash on Linux chokes on CR; PowerShell here-strings are CRLF on Windows.
$remoteScript = $remoteScript -replace "`r`n", "`n"

$output = $remoteScript | & gh codespace ssh -c $Codespace -- bash
if ($LASTEXITCODE) { throw "Remote stop_workers_local.sh failed (exit $LASTEXITCODE):`n$($output | Out-String)" }
$output | Write-Host

# Surface the resolved run id (in case we passed 'latest').
$resolvedLine = ($output -split "`r?`n") | Where-Object { $_ -like '__TM_RUN=*' } | Select-Object -First 1
if ($resolvedLine) { $Run = ($resolvedLine -replace '^__TM_RUN=', '').Trim() }

Write-Host ""
Write-Host "Codespace $Codespace : run $Run stopped."
