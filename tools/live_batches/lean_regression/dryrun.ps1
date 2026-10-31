# Offline check of the lean regression batch: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\lean_regression\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch lean_regression -Out $Out -MaxMinutes 20 -MaxMB 2048
exit $LASTEXITCODE
