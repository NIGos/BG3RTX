# Offline check of the smoke_ultra batch (one job): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\smoke_ultra\dryrun.ps1 [-Out <scratch dir>] [-Force]
param([string]$Out = '', [switch]$Force)
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch smoke_ultra -Out $Out -MaxMinutes 5 -MaxMB 512 -Force:$Force
exit $LASTEXITCODE
