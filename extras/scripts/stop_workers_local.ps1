<#
.SYNOPSIS
  Stop tm-worker instances spawned by start_workers_local.ps1.

.PARAMETER Run
  Run ID (e.g. 20260427-153012) or "latest". Required.

.PARAMETER GraceSeconds
  Seconds to wait for graceful exit before force-killing. Default 5.

.EXAMPLE
  .\stop_workers_local.ps1 -Run latest
#>
[CmdletBinding(PositionalBinding=$false)]
param(
    [Parameter(Mandatory=$true)]
    [string]$Run,

    [int]$GraceSeconds = 5
)

$ErrorActionPreference = 'Stop'

$cacheRoot = Join-Path $env:LOCALAPPDATA "tm-worker-farm\runs"

if ($Run -eq 'latest') {
    $latestPath = Join-Path $cacheRoot "latest.txt"
    if (-not (Test-Path -LiteralPath $latestPath)) {
        throw "No latest run pointer at $latestPath"
    }
    $Run = (Get-Content -LiteralPath $latestPath -Raw).Trim()
}

$runDir       = Join-Path $cacheRoot $Run
$manifestPath = Join-Path $runDir "manifest.json"
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Manifest not found: $manifestPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

# Phase 1: graceful CloseMainWindow / Stop-Process (Windows has no SIGTERM).
foreach ($w in $manifest.workers) {
    $procId = [int]$w.pid
    $proc = Get-Process -Id $procId -ErrorAction SilentlyContinue
    if (-not $proc) {
        Write-Host "[$($w.id)] PID $procId already gone."
        continue
    }
    Write-Host "[$($w.id)] Stopping PID $procId..."
    try {
        # Stop-Process without -Force still triggers a graceful close attempt
        # for console apps via Ctrl-Break is unreliable; CloseMainWindow only
        # works for windowed processes. tm-worker --noui is a console app, so
        # we go straight to Stop-Process and then escalate.
        Stop-Process -Id $procId -ErrorAction Stop
    } catch {
        Write-Warning "[$($w.id)] Stop-Process failed: $($_.Exception.Message)"
    }
}

# Phase 2: wait, then -Force any survivors.
$deadline = (Get-Date).AddSeconds($GraceSeconds)
while ((Get-Date) -lt $deadline) {
    $alive = $manifest.workers | Where-Object { Get-Process -Id ([int]$_.pid) -ErrorAction SilentlyContinue }
    if (-not $alive) { break }
    Start-Sleep -Milliseconds 250
}

foreach ($w in $manifest.workers) {
    $procId = [int]$w.pid
    if (Get-Process -Id $procId -ErrorAction SilentlyContinue) {
        Write-Warning "[$($w.id)] PID $procId still alive after ${GraceSeconds}s; forcing."
        Stop-Process -Id $procId -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
Write-Host "Run $Run stopped."
Write-Host "Logs and manifest preserved at: $runDir"
