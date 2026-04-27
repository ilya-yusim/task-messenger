<#
.SYNOPSIS
    One-time GCP setup for the GitHub Actions rendezvous deployer.

.DESCRIPTION
    Creates the github-deployer service account, grants it the minimum IAM
    roles needed by .github/workflows/deploy-rendezvous.yml, sets up a
    Workload Identity Federation pool + provider, and binds it to the
    GitHub repository so OIDC tokens from the repo can impersonate the SA.

    Idempotent: each step checks for existing resources and skips if present.
    Safe to re-run.

    On success, prints the values to plug into GitHub repo Variables:
      - GCP_DEPLOY_SA
      - GCP_WIF_PROVIDER

.PARAMETER Project
    GCP project ID (e.g. task-messenger-prod).

.PARAMETER Owner
    GitHub owner (user or org) of the task-messenger repo.

.PARAMETER Repo
    Repository name. Defaults to "task-messenger".

.PARAMETER PoolId
    Workload Identity pool ID. Defaults to "github".

.PARAMETER ProviderId
    Workload Identity provider ID. Defaults to "github".

.PARAMETER ServiceAccountId
    Short name for the service account. Defaults to "github-deployer".

.EXAMPLE
    .\extras\scripts\setup_gcp_deployer.ps1 -Project task-messenger-prod -Owner my-gh-user
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)] [string]$Project,
    [Parameter(Mandatory=$true)] [string]$Owner,
    [string]$Repo = "task-messenger",
    [string]$PoolId = "github",
    [string]$ProviderId = "github",
    [string]$ServiceAccountId = "github-deployer"
)

$ErrorActionPreference = "Stop"

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Skip($msg) { Write-Host "    (skip) $msg" -ForegroundColor DarkGray }
function Write-OK($msg)   { Write-Host "    OK $msg" -ForegroundColor Green }

# Resolve gcloud.cmd (the batch wrapper). The .ps1 wrapper that ships with
# Google Cloud SDK mangles arguments when invoked via splatting (@args), so
# we always go through the .cmd wrapper which passes argv natively.
$gcloudPs1 = (Get-Command gcloud -ErrorAction SilentlyContinue).Source
if (-not $gcloudPs1) {
    throw "gcloud CLI not found in PATH. Install Google Cloud SDK first."
}
$gcloud = Join-Path (Split-Path $gcloudPs1 -Parent) "gcloud.cmd"
if (-not (Test-Path $gcloud)) {
    # Some SDK installs only ship gcloud.ps1; fall back and hope for the best.
    $gcloud = $gcloudPs1
}

# Run gcloud with the given argv, throw if its exit code is non-zero.
function Invoke-Gcloud {
    [CmdletBinding(PositionalBinding=$false)]
    param(
        [Parameter(Mandatory)] [string]$Description,
        [Parameter(Mandatory, ValueFromRemainingArguments)] [string[]]$GcloudArgs
    )
    & $gcloud @GcloudArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Failed ($LASTEXITCODE): $Description"
    }
}

# Same, but retry up to N times with a delay — used when waiting for IAM
# eventual consistency (newly created SAs/roles can take a few seconds to
# become visible to add-iam-policy-binding).
function Invoke-GcloudWithRetry {
    [CmdletBinding(PositionalBinding=$false)]
    param(
        [Parameter(Mandatory)] [string]$Description,
        [Parameter()] [int]$MaxAttempts = 6,
        [Parameter()] [int]$DelaySeconds = 5,
        [Parameter(Mandatory, ValueFromRemainingArguments)] [string[]]$GcloudArgs
    )
    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        & $gcloud @GcloudArgs
        if ($LASTEXITCODE -eq 0) { return }
        if ($attempt -lt $MaxAttempts) {
            Write-Host "    attempt $attempt failed; retrying in ${DelaySeconds}s..." -ForegroundColor DarkYellow
            Start-Sleep -Seconds $DelaySeconds
        }
    }
    throw "Failed after $MaxAttempts attempts: $Description"
}

# Run gcloud and return $true if it exits 0, $false otherwise. Suppresses
# all output, including stderr. Needed for "does it exist?" checks because
# under $ErrorActionPreference=Stop, native commands writing to stderr will
# otherwise raise a NativeCommandError that not even *>$null can swallow.
function Test-GcloudSuccess {
    [CmdletBinding(PositionalBinding=$false)]
    param(
        [Parameter(Mandatory, ValueFromRemainingArguments)] [string[]]$GcloudArgs
    )
    $oldPref = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $gcloud @GcloudArgs *>$null
        return ($LASTEXITCODE -eq 0)
    } finally {
        $ErrorActionPreference = $oldPref
    }
}

# ── Sanity checks done above ────────────────────────────────────────────────

Write-Step "Setting active project to $Project"
Invoke-Gcloud -Description "set project" config set project $Project | Out-Null

# Required APIs for WIF impersonation + Compute / IAP SSH from GH Actions.
Write-Step "Enabling required GCP APIs"
Invoke-Gcloud -Description "enable APIs" `
    services enable `
    iamcredentials.googleapis.com `
    iam.googleapis.com `
    compute.googleapis.com `
    sts.googleapis.com `
    --project=$Project | Out-Null
Write-OK "APIs enabled"

$sa = "$ServiceAccountId@$Project.iam.gserviceaccount.com"
$repoFull = "$Owner/$Repo"

# ── 1. Service account ──────────────────────────────────────────────────────
Write-Step "Creating service account $sa"
if (Test-GcloudSuccess iam service-accounts describe $sa --format="value(email)") {
    Write-Skip "service account already exists"
} else {
    Invoke-Gcloud -Description "create service account" `
        iam service-accounts create $ServiceAccountId `
        "--display-name=GitHub Actions deployer" | Out-Null
    Write-OK "created $sa"
}

# ── 2. IAM roles ────────────────────────────────────────────────────────────
$roles = @(
    "roles/compute.instanceAdmin.v1",
    "roles/iap.tunnelResourceAccessor",
    "roles/compute.osLogin"
)
foreach ($role in $roles) {
    Write-Step "Granting $role to $sa"
    # Retry: when the SA was just created, IAM may still be propagating.
    Invoke-GcloudWithRetry -Description "add-iam-policy-binding $role" `
        projects add-iam-policy-binding $Project `
        "--member=serviceAccount:$sa" `
        "--role=$role" `
        "--condition=None" `
        "--quiet" | Out-Null
    Write-OK $role
}

# ── 2b. Allow the deployer to "act as" the VM's default compute SA ──────────
# `gcloud compute ssh` updates instance/project SSH metadata, which requires
# iam.serviceAccountUser on the SA attached to the VM (the default compute SA
# unless the VM was created with --service-account=<other>).
$projectNumForCompute = (& $gcloud projects describe $Project --format="value(projectNumber)").Trim()
if ($LASTEXITCODE -ne 0 -or -not $projectNumForCompute) {
    throw "Failed to resolve project number for $Project"
}
$computeSa = "$projectNumForCompute-compute@developer.gserviceaccount.com"
Write-Step "Granting roles/iam.serviceAccountUser on $computeSa to $sa"
Invoke-GcloudWithRetry -Description "actAs binding on compute SA" `
    iam service-accounts add-iam-policy-binding $computeSa `
    "--member=serviceAccount:$sa" `
    "--role=roles/iam.serviceAccountUser" `
    "--project=$Project" `
    "--quiet" | Out-Null
Write-OK "actAs binding applied"

# ── 3. Workload Identity pool ───────────────────────────────────────────────
Write-Step "Creating workload identity pool '$PoolId'"
if (Test-GcloudSuccess iam workload-identity-pools describe $PoolId --location=global --format="value(name)") {
    Write-Skip "pool already exists"
} else {
    Invoke-Gcloud -Description "create pool $PoolId" `
        iam workload-identity-pools create $PoolId `
        "--location=global" `
        "--display-name=GitHub Actions" | Out-Null
    Write-OK "created pool $PoolId"
}

# ── 4. OIDC provider scoped to the repo ─────────────────────────────────────
Write-Step "Creating OIDC provider '$ProviderId' in pool '$PoolId'"
if (Test-GcloudSuccess iam workload-identity-pools providers describe $ProviderId --location=global --workload-identity-pool=$PoolId --format="value(name)") {
    Write-Skip "provider already exists (not modifying attribute condition)"
} else {
    Invoke-Gcloud -Description "create OIDC provider $ProviderId" `
        iam workload-identity-pools providers create-oidc $ProviderId `
        "--location=global" `
        "--workload-identity-pool=$PoolId" `
        "--display-name=GitHub" `
        "--issuer-uri=https://token.actions.githubusercontent.com" `
        "--attribute-mapping=google.subject=assertion.sub,attribute.repository=assertion.repository" `
        "--attribute-condition=assertion.repository == '$repoFull'" | Out-Null
    Write-OK "created provider $ProviderId scoped to $repoFull"
}

# ── 5. Bind the GitHub repo to the SA ───────────────────────────────────────
Write-Step "Resolving project number"
$projectNum = & $gcloud projects describe $Project --format="value(projectNumber)"
if (-not $projectNum) { throw "could not resolve project number for $Project" }

$principalSet = "principalSet://iam.googleapis.com/projects/$projectNum/locations/global/workloadIdentityPools/$PoolId/attribute.repository/$repoFull"

Write-Step "Allowing $repoFull to impersonate $sa"
Invoke-GcloudWithRetry -Description "add-iam-policy-binding workloadIdentityUser" `
    iam service-accounts add-iam-policy-binding $sa `
    "--role=roles/iam.workloadIdentityUser" `
    "--member=$principalSet" `
    "--condition=None" `
    "--quiet" | Out-Null
Write-OK "binding applied"

# ── Summary ─────────────────────────────────────────────────────────────────
$wifProvider = "projects/$projectNum/locations/global/workloadIdentityPools/$PoolId/providers/$ProviderId"

Write-Host ""
Write-Host "================ GitHub repo Variables to set ================" -ForegroundColor Yellow
Write-Host "GCP_PROJECT       = $Project"
Write-Host "GCP_DEPLOY_SA     = $sa"
Write-Host "GCP_WIF_PROVIDER  = $wifProvider"
Write-Host "==============================================================" -ForegroundColor Yellow
Write-Host ""
Write-Host "Also set (per your environment):" -ForegroundColor Yellow
Write-Host "  GCP_ZONE                    (e.g. us-west1-a)"
Write-Host "  GCP_VM_NAME                 (e.g. tm-rendezvous)"
Write-Host "  RENDEZVOUS_HEALTHCHECK_URL  (e.g. https://taskmessenger-rdv.duckdns.org/healthz)"
Write-Host ""
Write-Host "Done." -ForegroundColor Green
