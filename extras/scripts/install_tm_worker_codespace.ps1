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

$remoteDir = '~/.local/share/tm-worker-farm'

Write-Host "Uploading installer to $Codespace ..."
& gh codespace ssh -c $Codespace -- "mkdir -p $remoteDir"
if ($LASTEXITCODE) { throw "Failed to create remote dir on $Codespace" }

& gh codespace cp -c $Codespace -e $installer "remote:$remoteDir/install_tm_worker_release.sh"
if ($LASTEXITCODE) { throw "Failed to upload installer" }

& gh codespace ssh -c $Codespace -- "chmod +x $remoteDir/install_tm_worker_release.sh"
if ($LASTEXITCODE) { throw "Failed to chmod installer" }

Write-Host "Running installer (repo=$Repo tag=$Tag) on $Codespace ..."
$remoteCmd = "$remoteDir/install_tm_worker_release.sh -r '$Repo' -t '$Tag'"
& gh codespace ssh -c $Codespace -- $remoteCmd
if ($LASTEXITCODE) { throw "Remote installer failed (exit $LASTEXITCODE)" }

Write-Host ""
Write-Host "tm-worker installed on $Codespace."
Write-Host "Next:  .\start_workers_codespace.ps1 -Codespace $Codespace -Count <N>"
