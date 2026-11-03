param([string]$Out = '')
$ErrorActionPreference = 'Stop'
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
  $shim = Join-Path $env:TEMP 'bg3rtx-hair-python-shim'
  New-Item -ItemType Directory -Force -Path $shim | Out-Null
  Set-Content -LiteralPath (Join-Path $shim 'py.ps1') '& python @($args | Where-Object { $_ -ne ''-3'' })'
  $env:PATH = "$shim;$env:PATH"
}
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch hair_validation -Out $Out -MaxMinutes 30 -MaxMB 2048 -TraceMB 30 -MaxBursts 6 -BurstMB 165 -Independent -Unfocused
exit $LASTEXITCODE
