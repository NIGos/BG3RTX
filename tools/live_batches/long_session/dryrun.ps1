# Offline check of the long_session batch (one 37-minute job): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\long_session\dryrun.ps1 [-Out <scratch dir>] [-Force]
param([string]$Out = '', [switch]$Force)
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch long_session -Out $Out -MaxMinutes 45 -MaxMB 128 -Force:$Force
exit $LASTEXITCODE
