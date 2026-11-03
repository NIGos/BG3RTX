# Offline check of the smoke_ultra batch (one job): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\smoke_ultra\dryrun.ps1 [-Out <scratch dir>] [-Force]
param([string]$Out = '', [switch]$Force)
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
  $shim = Join-Path $env:TEMP 'bg3rtx-smoke-ultra-python-shim'
  New-Item -ItemType Directory -Force $shim | Out-Null
  Set-Content -LiteralPath (Join-Path $shim 'py.ps1') '& python @($args | Where-Object { $_ -ne ''-3'' })'
  $env:PATH = "$shim;$env:PATH"
}
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch smoke_ultra -Out $Out -MaxMinutes 6 -MaxMB 512 -Force:$Force
exit $LASTEXITCODE
