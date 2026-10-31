# Offline check of the pool lag batch: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\pool_lag\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch pool_lag -Out $Out -MaxMinutes 6 -MaxMB 64
exit $LASTEXITCODE
