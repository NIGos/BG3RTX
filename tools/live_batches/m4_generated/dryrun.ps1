# Offline check of the m4_generated batch draft: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\m4_generated\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch m4_generated -Out $Out -MaxMinutes 10 -MaxMB 1024 -Independent
exit $LASTEXITCODE
