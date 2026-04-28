<#
.SYNOPSIS
  Install tm-worker into a GitHub Codespace from a published release.

.DESCRIPTION
  Uploads install_tm_worker_release.sh to the codespace and runs it. The
  installer fetches the requested release's makeself .run asset and
  installs to ~/.local/share/task-messenger/tm-worker, with the binary
  symlinked at ~/.local/bin/tm-worker.

  After this completes, start_workers_codespace.ps1 will find tm-worker
  on PATH (its default RemoteWorkerBin is "tm-worker"; if the codespace's
  login shell already has ~/.local/bin on PATH this Just Works).

.PARAMETER Codespace
  Codespace name. If omitted, picks the first running codespace from
  `gh codespace list`.

.PARAMETER Tag
  Release tag (e.g. v0.4.2). Default: "latest".

.PARAMETER Repo
  Override repo (OWNER/REPO). Default: inferred from local `git remote`,
  falling back to ilya-yusim/task-messenger.

.EXAMPLE
  .\install_tm_worker_codespace.ps1
  .\install_tm_worker_codespace.ps1 -Codespace fluffy-disco-abc123 -Tag v0.4.2
#>
[CmdletBinding(PositionalBinding=$false)]
param(
    [string]$Codespace,
    [string]$Tag = "latest",
    [string]$Repo
)

$ErrorActionPreference = 'Stop'

$gh = Get-Command gh -ErrorAction SilentlyContinue
if (-not $gh) { throw "gh CLI not found in PATH. Install from https://cli.github.com/" }

# Pick a codespace if not given. Accept any state — gh codespace ssh will
# auto-resume a Shutdown codespace.
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
    # Prefer a running one; otherwise take the first (will auto-resume).
    $running = $list | Where-Object { $_.state -in @('Available','Running') }
    if ($running) {
        $Codespace = $running[0].name
    } else {
        $Codespace = $list[0].name
        Write-Warning "No running codespace; will resume '$Codespace' (state: $($list[0].state)). This can take ~30s."
    }
    Write-Host "Using codespace: $Codespace"
}

# Resolve repo if not given.
if (-not $Repo) {
    try {
        $remote = & git -C $PSScriptRoot remote get-url origin 2>$null
        if ($remote -match 'github\.com[:/]+([^/]+/[^/.]+)(?:\.git)?$') {
            $Repo = $Matches[1]
        }
    } catch {}
    if (-not $Repo) { $Repo = 'ilya-yusim/task-messenger' }
}

$installer = Join-Path $PSScriptRoot 'install_tm_worker_release.sh'
if (-not (Test-Path -LiteralPath $installer)) {
    throw "Helper not found: $installer"
}

# Resolve the asset locally — the codespace's gh is typically not authed for
# foreign-owner repos and draft releases are invisible without auth. Doing
# the resolve+download here (where the user's gh IS authed) sidesteps both.
Write-Host "Resolving release asset for $Repo $Tag ..."
if ($Tag -eq 'latest') {
    $Tag = (& gh release view -R $Repo --json tagName --jq .tagName 2>$null).Trim()
    if (-not $Tag) { throw "Could not resolve latest release for $Repo (is gh authed?)" }
    Write-Host "  resolved latest -> $Tag"
}

$assetsJson = & gh release view -R $Repo $Tag --json assets 2>&1
if ($LASTEXITCODE) {
    throw "gh release view failed for ${Repo} ${Tag}: $($assetsJson | Out-String)"
}
$assets = ($assetsJson | ConvertFrom-Json).assets
$workerAsset = $assets | Where-Object { $_.name -match '^tm-worker-v.*-linux-x86_64\.run$' } | Select-Object -First 1
if (-not $workerAsset) {
    throw "No tm-worker-v*-linux-x86_64.run asset found on $Repo $Tag. Available: $($assets.name -join ', ')"
}
Write-Host "  asset: $($workerAsset.name)"

$localTmp = Join-Path ([System.IO.Path]::GetTempPath()) ("tm-worker-asset-" + [System.IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $localTmp | Out-Null
try {
    Write-Host "Downloading $($workerAsset.name) locally ..."
    & gh release download -R $Repo $Tag --pattern $workerAsset.name --dir $localTmp
    if ($LASTEXITCODE) { throw "gh release download failed" }
    $localAsset = Join-Path $localTmp $workerAsset.name
    if (-not (Test-Path -LiteralPath $localAsset)) { throw "Downloaded asset not found at $localAsset" }

    $remoteDir = '~/.local/share/tm-worker-farm'

    Write-Host "Uploading installer + asset to $Codespace ..."
    & gh codespace ssh -c $Codespace -- "mkdir -p $remoteDir"
    if ($LASTEXITCODE) { throw "Failed to create remote dir on $Codespace" }

    & gh codespace cp -c $Codespace -e $installer "remote:$remoteDir/install_tm_worker_release.sh"
    if ($LASTEXITCODE) { throw "Failed to upload installer" }

    & gh codespace cp -c $Codespace -e $localAsset "remote:$remoteDir/$($workerAsset.name)"
    if ($LASTEXITCODE) { throw "Failed to upload .run asset" }

    & gh codespace ssh -c $Codespace -- "chmod +x $remoteDir/install_tm_worker_release.sh $remoteDir/$($workerAsset.name)"
    if ($LASTEXITCODE) { throw "Failed to chmod uploaded files" }

    Write-Host "Running installer on $Codespace (using pre-staged $($workerAsset.name)) ..."
    $remoteCmd = "$remoteDir/install_tm_worker_release.sh -f $remoteDir/$($workerAsset.name)"
    & gh codespace ssh -c $Codespace -- $remoteCmd
    if ($LASTEXITCODE) { throw "Remote installer failed (exit $LASTEXITCODE)" }
} finally {
    Remove-Item -Recurse -Force -LiteralPath $localTmp -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "tm-worker installed on $Codespace."
Write-Host "Next:  .\start_workers_codespace.ps1 -Codespace $Codespace -Count <N>"
