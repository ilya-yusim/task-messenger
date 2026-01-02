param(
    [Parameter(Mandatory=$true)]
    [int]$Count
)

if ($Count -lt 1) {
    Write-Error "Count must be 1 or greater"
    exit 1
}

$exe = Join-Path $PSScriptRoot "..\builddir-worker\worker\worker.exe"
$exe = [System.IO.Path]::GetFullPath($exe)

for ($i = 1; $i -le $Count; $i++) {
    Write-Output "Starting instance ${i} of ${Count}: ${exe}"
    Start-Process -FilePath $exe -ArgumentList '-c config-worker.json --mode blocking'
}
