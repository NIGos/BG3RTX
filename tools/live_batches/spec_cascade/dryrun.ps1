# Offline check of the spec_cascade batch (one job): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\spec_cascade\dryrun.ps1 [-Out <scratch dir>] [-Force]
param([string]$Out = '', [switch]$Force)
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch spec_cascade -Out $Out -MaxMinutes 7 -MaxMB 1024 -Force:$Force
exit $LASTEXITCODE
