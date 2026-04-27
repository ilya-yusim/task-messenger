<#
.SYNOPSIS
    Creates the tm-rendezvous GCP VM with the cloud-init bootstrap.

    The VM is provisioned with Caddy + DuckDNS only. The tm-rendezvous binary
    itself is installed by the GitHub Actions "Deploy Rendezvous to GCP"
    workflow on the next release / workflow_dispatch.

.PARAMETER DuckdnsToken
    DuckDNS API token (required).

.PARAMETER Project
.PARAMETER Zone
.PARAMETER Name
.PARAMETER Repo
.PARAMETER Domain
.PARAMETER DashboardPort

.EXAMPLE
    .\extras\scripts\create_rendezvous_vm.ps1 -DuckdnsToken <token>
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$DuckdnsToken,
    [string]$Project       = "task-messenger-prod",
    [string]$Zone          = "us-west1-a",
    [string]$Name          = "tm-rendezvous",
    [string]$Repo          = "ilya-yusim/task-messenger",
    [string]$Domain        = "taskmessenger-rdv",
    [int]   $DashboardPort = 8080
)

$ErrorActionPreference = "Stop"

# Resolve cloud-init file relative to this script.
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$userData  = Join-Path $scriptDir "cloud-init-rendezvous.yaml"
if (-not (Test-Path $userData)) {
    throw "cloud-init file not found at $userData"
}

$metadata = @(
    "duckdns-domain=$Domain",
    "duckdns-token=$DuckdnsToken",
    "rendezvous-repo=$Repo",
    "rendezvous-dashboard-port=$DashboardPort"
) -join ","

Write-Host "==> Creating VM $Name in $Project / $Zone"
Write-Host "    (tm-rendezvous binary will be installed by the GH Actions deploy workflow)"

& gcloud compute instances create $Name `
    --project=$Project `
    --zone=$Zone `
    --machine-type=e2-micro `
    --image-family=ubuntu-2404-lts-amd64 `
    --image-project=ubuntu-os-cloud `
    --boot-disk-size=30GB `
    --boot-disk-type=pd-standard `
    --tags="http-server,https-server" `
    --metadata-from-file="user-data=$userData" `
    --metadata="$metadata"

if ($LASTEXITCODE -ne 0) {
    throw "gcloud compute instances create failed with exit code $LASTEXITCODE"
}

Write-Host ""
Write-Host "VM created. Watch bootstrap with:"
Write-Host "  gcloud compute ssh $Name --zone=$Zone --tunnel-through-iap --command 'sudo tail -f /var/log/cloud-init-output.log'"
Write-Host ""
Write-Host "After cloud-init finishes, trigger a deploy:"
Write-Host "  Actions tab -> 'Deploy Rendezvous to GCP' -> Run workflow"
Write-Host "  (or publish a non-prerelease GitHub Release)."
