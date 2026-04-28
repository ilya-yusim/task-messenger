<#
.SYNOPSIS
  Start N tm-worker instances locally, recording a run manifest so they can
  later be stopped via stop_workers_local.ps1.

.DESCRIPTION
  Each worker runs headless (--noui), with stdout+stderr redirected to a
  per-worker log file. A manifest.json describing the run is written under:

      $env:LOCALAPPDATA\tm-worker-farm\runs\<run-id>\

  The directory is also printed at the end and returned in the manifest's
  `base_dir` field. To stop all workers from a run:

      .\stop_workers_local.ps1 -Run <run-id>

  Or pass -Run latest to target the most recent run.

.PARAMETER Count
  Number of worker instances to start. Required.

.PARAMETER WorkerBin
  Path to tm-worker.exe. Defaults to ..\..\builddir\worker\tm-worker.exe
  relative to this script.

.PARAMETER Config
  Path to config-worker.json. Defaults to ..\..\config\config-worker.json.

.PARAMETER ExtraArgs
  Additional arguments to pass to every worker (after the config + mode +
  --noui defaults). Use a single quoted string, e.g. -ExtraArgs "--mode async".

.EXAMPLE
  .\start_workers_local.ps1 -Count 3
#>
[CmdletBinding(PositionalBinding=$false)]
param(
    [Parameter(Mandatory=$true)]
    [int]$Count,

    [string]$WorkerBin,
    [string]$Config,
    [string]$ExtraArgs = ""
)

$ErrorActionPreference = 'Stop'

if ($Count -lt 1) {
    throw "Count must be 1 or greater"
}

# Resolve defaults relative to this script.
if (-not $WorkerBin) {
    $WorkerBin = Join-Path $PSScriptRoot "..\..\builddir\worker\tm-worker.exe"
}
if (-not $Config) {
    $Config = Join-Path $PSScriptRoot "..\..\config\config-worker.json"
}
$WorkerBin = [System.IO.Path]::GetFullPath($WorkerBin)
$Config    = [System.IO.Path]::GetFullPath($Config)

if (-not (Test-Path -LiteralPath $WorkerBin)) {
    throw "Worker executable not found: $WorkerBin"
}
if (-not (Test-Path -LiteralPath $Config)) {
    throw "Worker config not found: $Config"
}

# Run dir: $env:LOCALAPPDATA\tm-worker-farm\runs\<run-id>\
$cacheRoot = Join-Path $env:LOCALAPPDATA "tm-worker-farm\runs"
$runId     = (Get-Date).ToString("yyyyMMdd-HHmmss")
$runDir    = Join-Path $cacheRoot $runId
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

# Compose worker arg list.
$baseArgs = @('-c', $Config, '--mode', 'blocking', '--noui')
if ($ExtraArgs.Trim()) {
    # Split on whitespace, ignoring empty entries. Caller is responsible for
    # quoting any args that contain spaces (rare for tm-worker).
    $extra = $ExtraArgs -split '\s+' | Where-Object { $_ -ne '' }
    $baseArgs += $extra
}

$workers = @()
for ($i = 1; $i -le $Count; $i++) {
    $id      = ('{0:D2}' -f $i)
    $logPath = Join-Path $runDir "worker-$id.log"
    $pidPath = Join-Path $runDir "worker-$id.pid"

    Write-Host "[$id/$Count] Starting: $WorkerBin $($baseArgs -join ' ')"
    $proc = Start-Process -FilePath $WorkerBin `
                          -ArgumentList $baseArgs `
                          -RedirectStandardOutput $logPath `
                          -RedirectStandardError "$logPath.err" `
                          -WindowStyle Hidden `
                          -PassThru
    Set-Content -LiteralPath $pidPath -Value $proc.Id -Encoding ASCII

    $workers += [pscustomobject]@{
        id       = $id
        pid      = $proc.Id
        log      = $logPath
        log_err  = "$logPath.err"
        pidfile  = $pidPath
    }
}

$manifest = [ordered]@{
    run_id     = $runId
    started_at = (Get-Date).ToUniversalTime().ToString("o")
    host       = "local"
    os         = "windows"
    base_dir   = $runDir
    worker_bin = $WorkerBin
    config     = $Config
    args       = $baseArgs
    workers    = $workers
}

$manifestPath = Join-Path $runDir "manifest.json"
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

# Update the convenient "latest" pointer (best-effort; ignore errors).
try {
    $latestPath = Join-Path $cacheRoot "latest.txt"
    Set-Content -LiteralPath $latestPath -Value $runId -Encoding ASCII
} catch {}

Write-Host ""
Write-Host "Run ID:   $runId"
Write-Host "Run dir:  $runDir"
Write-Host "Manifest: $manifestPath"
Write-Host ""
Write-Host "To stop:  .\stop_workers_local.ps1 -Run $runId"
