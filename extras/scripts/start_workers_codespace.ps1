<#
.SYNOPSIS
  Start N tm-worker instances inside a GitHub Codespace, recording a local
  pointer to the remote run.

.DESCRIPTION
  Uploads start_workers_local.sh + stop_workers_local.sh to the codespace
  (under ~/.local/share/tm-worker-farm/), runs the start script there, and
  fetches the resulting manifest.json back to:

      $env:LOCALAPPDATA\tm-worker-farm\runs\codespace-<name>\<run-id>\manifest.json

  Requires the GitHub CLI (`gh`) to be installed and authenticated. The
  codespace must already have tm-worker built or installed. By default the
  remote scripts look for the binary at:

      ~/task-messenger/builddir/worker/tm-worker

  Use -RemoteWorkerBin to override.

.PARAMETER Codespace
  Codespace name. If omitted, picks the first running codespace from
  `gh codespace list`.

.PARAMETER Count
  Number of workers to start. Required.

.PARAMETER RemoteWorkerBin
  Path to tm-worker on the codespace. Default: "tm-worker" (assumes the
  installer's symlink at ~/.local/bin/tm-worker is on the login PATH;
  install via install_tm_worker_codespace.ps1).

.PARAMETER RemoteConfig
  Absolute path to config-worker.json on the codespace. Default:
  ~/.config/task-messenger/tm-worker/config-worker.json (where the .run
  installer places it). Falls back gracefully if unset by the worker.

.EXAMPLE
  .\start_workers_codespace.ps1 -Count 3
  .\start_workers_codespace.ps1 -Codespace fluffy-disco-abc123 -Count 5
#>
[CmdletBinding(PositionalBinding=$false)]
param(
    [Parameter(Mandatory=$true)]
    [int]$Count,

    [string]$Codespace,
    [string]$RemoteWorkerBin = 'tm-worker',
    [string]$RemoteConfig    = '~/.config/task-messenger/tm-worker/config-worker.json'
)

$ErrorActionPreference = 'Stop'

if ($Count -lt 1) { throw "Count must be 1 or greater" }

# Locate gh.
$gh = (Get-Command gh -ErrorAction SilentlyContinue)
if (-not $gh) { throw "gh CLI not found in PATH. Install from https://cli.github.com/" }

# Pick a codespace if one wasn't passed. Accept any state — gh codespace
# ssh will auto-resume a Shutdown codespace.
if (-not $Codespace) {
    $listOutput = & gh codespace list --json name,state 2>&1
    $listJson = ($listOutput | Where-Object { $_ -is [string] -and $_.StartsWith('[') }) -join "`n"
    if (-not $listJson.Trim()) {
        $msg = ($listOutput | Out-String).Trim()
        if ($msg -match '403' -or $msg -match 'admin rights') {
            throw "gh can't list codespaces: $msg`nYour gh auth is missing the 'codespace' scope or is logged into the wrong account. Try:`n  gh auth refresh -h github.com -s codespace`nOr re-login: gh auth login --scopes codespace"
        }
        throw "Failed to list codespaces (gh not logged in?): $msg"
    }
    $list = $listJson | ConvertFrom-Json
    if (-not $list) { throw "No codespaces found. Create one with 'gh codespace create' first." }
    $running = $list | Where-Object { $_.state -in @('Available','Running') }
    if ($running) {
        $Codespace = $running[0].name
    } else {
        $Codespace = $list[0].name
        Write-Warning "No running codespace; will resume '$Codespace' (state: $($list[0].state)). This can take ~30s."
    }
    Write-Host "Using codespace: $Codespace"
}

$scriptDir   = $PSScriptRoot
$startScript = Join-Path $scriptDir "start_workers_local.sh"
$stopScript  = Join-Path $scriptDir "stop_workers_local.sh"
foreach ($s in @($startScript, $stopScript)) {
    if (-not (Test-Path -LiteralPath $s)) { throw "Missing helper: $s" }
}

# --- Round trip 1: upload both helpers in a single cp call. ---
# Drop them in $HOME (always exists); the ssh below moves them into place.
# This avoids a separate `mkdir` round trip.
Write-Host "Uploading helper scripts to $Codespace ..."
& gh codespace cp -c $Codespace -e $startScript $stopScript "remote:."
if ($LASTEXITCODE) { throw "Failed to upload helper scripts" }

# --- Round trip 2: do everything else in one ssh. ---
# Piping a bash script over stdin lets us run mkdir + mv + chmod + the
# start helper + emit `$HOME, run-id, and the manifest contents (with
# delimiters) without paying the codespace ssh setup cost 6 more times.
# Avoid `bash -lc`: some images run an `nvs` auto-loader from the login
# profile that misinterprets our `-n N` flag.
Write-Host "Starting $Count worker(s) on $Codespace ..."
$remoteScript = @'
set -e
REMOTE_DIR="$HOME/.local/share/tm-worker-farm"
mkdir -p "$REMOTE_DIR"
mv -f "$HOME/start_workers_local.sh" "$HOME/stop_workers_local.sh" "$REMOTE_DIR/"
chmod +x "$REMOTE_DIR/start_workers_local.sh" "$REMOTE_DIR/stop_workers_local.sh"
"$REMOTE_DIR/start_workers_local.sh" -n __COUNT__ -b '__BIN__' -c '__CONFIG__'
run_id=$(cat "$HOME/.cache/tm-worker-farm/runs/latest.txt")
printf '__TM_HOME=%s\n' "$HOME"
printf '__TM_RUN=%s\n'  "$run_id"
echo '__TM_MANIFEST_BEGIN'
cat "$HOME/.cache/tm-worker-farm/runs/$run_id/manifest.json"
echo
echo '__TM_MANIFEST_END'
'@
$remoteScript = $remoteScript `
    -replace '__COUNT__',  [string]$Count `
    -replace '__BIN__',    $RemoteWorkerBin `
    -replace '__CONFIG__', $RemoteConfig
# bash on Linux chokes on CR; PowerShell here-strings are CRLF on Windows.
$remoteScript = $remoteScript -replace "`r`n", "`n"

$output = $remoteScript | & gh codespace ssh -c $Codespace -- bash
if ($LASTEXITCODE) { throw "Remote bootstrap failed (exit $LASTEXITCODE):`n$($output | Out-String)" }

# Parse markers out of the combined output.
$lines = $output -split "`r?`n"
$remoteHome  = (($lines | Where-Object { $_ -like '__TM_HOME=*' } | Select-Object -First 1) -replace '^__TM_HOME=', '').Trim()
$remoteRunId = (($lines | Where-Object { $_ -like '__TM_RUN=*'  } | Select-Object -First 1) -replace '^__TM_RUN=',  '').Trim()
if (-not $remoteHome)  { throw "Did not see __TM_HOME marker in remote output" }
if (-not $remoteRunId) { throw "Did not see __TM_RUN marker in remote output" }

$beginIdx = [array]::IndexOf($lines, '__TM_MANIFEST_BEGIN')
$endIdx   = [array]::IndexOf($lines, '__TM_MANIFEST_END')
if ($beginIdx -lt 0 -or $endIdx -lt 0 -or $endIdx -le $beginIdx + 1) {
    throw "Could not extract manifest from remote output"
}
$manifestJson = ($lines[($beginIdx + 1)..($endIdx - 1)] -join "`n").Trim()

# Mirror the manifest locally so stop_workers_codespace can find it.
$localBase = Join-Path $env:LOCALAPPDATA "tm-worker-farm\runs\codespace-$Codespace\$remoteRunId"
New-Item -ItemType Directory -Force -Path $localBase | Out-Null
$tmpManifest = Join-Path $localBase "manifest.json"
Set-Content -LiteralPath $tmpManifest -Value $manifestJson -Encoding UTF8

# Update local "latest" pointer for this codespace.
$cacheRoot = Join-Path $env:LOCALAPPDATA "tm-worker-farm\runs\codespace-$Codespace"
Set-Content -LiteralPath (Join-Path $cacheRoot "latest.txt") -Value $remoteRunId -Encoding ASCII

Write-Host ""
Write-Host "Codespace: $Codespace"
Write-Host "Remote run ID: $remoteRunId"
Write-Host "Local manifest copy: $tmpManifest"
Write-Host ""
Write-Host "To stop:  .\stop_workers_codespace.ps1 -Codespace $Codespace -Run $remoteRunId"
