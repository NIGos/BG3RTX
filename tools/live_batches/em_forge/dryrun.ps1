# Offline check of the D-EM Adamantine Forge batch: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\em_forge\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch em_forge -Out $Out -MaxMinutes 10 -MaxMB 512
exit $LASTEXITCODE
