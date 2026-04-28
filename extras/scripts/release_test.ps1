<#
.SYNOPSIS
  Trigger the Release workflow for a single OS/component, wait for it to
  finish, and print the resulting release tag.

.DESCRIPTION
  Uses `gh workflow run release.yml` with the workflow_dispatch inputs
  filtering matrix down to one (os, component) cell. Tag releases are not
  created on workflow_dispatch; instead, the workflow publishes an
  "untagged" release named `draft-<sha>` with the artifacts attached.
  This script discovers that tag after the run completes so you can
  pass it straight to install_tm_worker_codespace.ps1.

.PARAMETER Os
  GitHub runner OS. One of: ubuntu-latest (default), windows-latest, macos.

.PARAMETER Component
  Component to build. One of: worker (default), dispatcher, rendezvous.

.PARAMETER NoWait
  Don't wait for the workflow run to complete; just trigger it.

.EXAMPLE
  .\release_test.ps1
  .\release_test.ps1 -Os ubuntu-latest -Component worker
#>
[CmdletBinding(PositionalBinding=$false)]
param(
    [ValidateSet('ubuntu-latest','windows-latest','macos')]
    [string]$Os = 'ubuntu-latest',

    [ValidateSet('worker','dispatcher','rendezvous')]
    [string]$Component = 'worker',

    [switch]$NoWait
)

$ErrorActionPreference = 'Stop'

$gh = Get-Command gh -ErrorAction SilentlyContinue
if (-not $gh) { throw "gh CLI not found in PATH." }

# Capture the latest run id BEFORE dispatching so we can identify the new one.
$beforeId = & gh run list --workflow=release.yml --limit 1 --json databaseId `
            | ConvertFrom-Json | ForEach-Object { $_.databaseId }

Write-Host "Triggering Release workflow: os=$Os component=$Component"
& gh workflow run release.yml -f "os=$Os" -f "component=$Component"
if ($LASTEXITCODE) { throw "gh workflow run failed (exit $LASTEXITCODE)" }

# Poll for the new run (gh workflow run is async).
$runId = $null
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 2
    $latest = & gh run list --workflow=release.yml --limit 1 --json databaseId,status,headBranch `
              | ConvertFrom-Json | Select-Object -First 1
    if ($latest -and $latest.databaseId -ne $beforeId) {
        $runId = $latest.databaseId
        break
    }
}
if (-not $runId) { throw "Could not detect the new workflow run after 60s." }

Write-Host "Run id: $runId"
Write-Host "URL:     https://github.com/$(& gh repo view --json nameWithOwner -q .nameWithOwner)/actions/runs/$runId"

if ($NoWait) {
    Write-Host ""
    Write-Host "Skipping wait (-NoWait). Watch progress with: gh run watch $runId"
    return
}

Write-Host ""
Write-Host "Waiting for run to finish..."
& gh run watch $runId --exit-status
$watchExit = $LASTEXITCODE
if ($watchExit -ne 0) { throw "Workflow run $runId did not succeed (exit $watchExit). See URL above." }

# Discover the resulting release tag. workflow_dispatch creates an
# untagged release titled draft-<sha>. Find the most recent matching one.
Start-Sleep -Seconds 3   # let the API catch up
$rel = & gh release list --limit 10 --json tagName,name,createdAt,isDraft `
       | ConvertFrom-Json `
       | Where-Object { $_.name -like 'draft-*' -or $_.tagName -like 'draft-*' } `
       | Sort-Object createdAt -Descending `
       | Select-Object -First 1

if (-not $rel) {
    Write-Warning "Workflow finished but no draft-* release found. Check the Releases page."
    return
}

Write-Host ""
Write-Host "Release tag: $($rel.tagName)"
Write-Host "Release name: $($rel.name)"
Write-Host ""
Write-Host "Install on a codespace with:"
Write-Host "  .\extras\scripts\install_tm_worker_codespace.ps1 -Tag $($rel.tagName)"
