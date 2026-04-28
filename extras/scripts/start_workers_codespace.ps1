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

$remoteDir = '~/.local/share/tm-worker-farm'

# Upload helpers.
Write-Host "Uploading helper scripts to $Codespace ..."
& gh codespace ssh -c $Codespace -- "mkdir -p $remoteDir"
if ($LASTEXITCODE) { throw "Failed to create remote dir on $Codespace" }

& gh codespace cp -c $Codespace -e $startScript "remote:$remoteDir/start_workers_local.sh"
if ($LASTEXITCODE) { throw "Failed to upload start script" }
& gh codespace cp -c $Codespace -e $stopScript  "remote:$remoteDir/stop_workers_local.sh"
if ($LASTEXITCODE) { throw "Failed to upload stop script" }

& gh codespace ssh -c $Codespace -- "chmod +x $remoteDir/start_workers_local.sh $remoteDir/stop_workers_local.sh"
if ($LASTEXITCODE) { throw "Failed to chmod remote scripts" }

# Run remote start. We deliberately avoid `bash -lc` here: some codespace
# images run an `nvs` auto-loader from their login profile that scans the
# command line and treats our `-n 2` flag as a Node version selector.
# `gh codespace ssh -- <cmd>` already runs through the user's default
# shell with ~/.local/bin on PATH, which is what we want.
Write-Host "Starting $Count worker(s) on $Codespace ..."
$remoteCmd = "$remoteDir/start_workers_local.sh -n $Count -b '$RemoteWorkerBin' -c '$RemoteConfig'"
& gh codespace ssh -c $Codespace -- $remoteCmd
if ($LASTEXITCODE) { throw "Remote start_workers_local.sh failed (exit $LASTEXITCODE)" }

# Discover the run-id the remote script just wrote.
$remoteRunId = & gh codespace ssh -c $Codespace -- 'cat $HOME/.cache/tm-worker-farm/runs/latest.txt'
if ($LASTEXITCODE) { throw "Could not read remote latest.txt" }
$remoteRunId = $remoteRunId.Trim()
if (-not $remoteRunId) { throw "Remote latest.txt was empty" }

# scp (used by `gh codespace cp`) does NOT expand $HOME or ~ on the remote
# side, so we need an absolute path. Resolve $HOME via ssh first.
$remoteHome = (& gh codespace ssh -c $Codespace -- 'printf %s "$HOME"').Trim()
if ($LASTEXITCODE -or -not $remoteHome) { throw "Could not resolve remote `$HOME on $Codespace" }

# Mirror the manifest locally so stop_workers_codespace can find it.
$localBase = Join-Path $env:LOCALAPPDATA "tm-worker-farm\runs\codespace-$Codespace\$remoteRunId"
New-Item -ItemType Directory -Force -Path $localBase | Out-Null

$remoteManifest = "$remoteHome/.cache/tm-worker-farm/runs/$remoteRunId/manifest.json"
$tmpManifest    = Join-Path $localBase "manifest.json"
& gh codespace cp -c $Codespace -e "remote:$remoteManifest" $tmpManifest
if ($LASTEXITCODE) { throw "Failed to fetch remote manifest" }

# Update local "latest" pointer for this codespace.
$cacheRoot = Join-Path $env:LOCALAPPDATA "tm-worker-farm\runs\codespace-$Codespace"
Set-Content -LiteralPath (Join-Path $cacheRoot "latest.txt") -Value $remoteRunId -Encoding ASCII

Write-Host ""
Write-Host "Codespace: $Codespace"
Write-Host "Remote run ID: $remoteRunId"
Write-Host "Local manifest copy: $tmpManifest"
Write-Host ""
Write-Host "To stop:  .\stop_workers_codespace.ps1 -Codespace $Codespace -Run $remoteRunId"
