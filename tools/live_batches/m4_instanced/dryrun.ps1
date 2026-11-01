# Offline check of the m4_instanced batch draft: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\m4_instanced\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch m4_instanced -Out $Out -MaxMinutes 8 -MaxMB 1024 -Independent
exit $LASTEXITCODE
