param(
    [Parameter(Mandatory=$true)]
    [int]$Count
)

if ($Count -lt 1) {
    Write-Error "Count must be 1 or greater"
    exit 1
}

$exe = Join-Path $PSScriptRoot "..\..\builddir\worker\tm-worker.exe"
$exe = [System.IO.Path]::GetFullPath($exe)

$config = Join-Path $PSScriptRoot "..\..\config\config-worker.json"
$config = [System.IO.Path]::GetFullPath($config)

if (-not (Test-Path -LiteralPath $exe)) {
    Write-Error "Worker executable not found: $exe"
    exit 1
}
if (-not (Test-Path -LiteralPath $config)) {
    Write-Error "Worker config not found: $config"
    exit 1
}

for ($i = 1; $i -le $Count; $i++) {
    Write-Output "Starting instance ${i} of ${Count}: ${exe} -c ${config} --mode blocking"
    Start-Process -FilePath $exe -ArgumentList @('-c', $config, '--mode', 'blocking')
}
