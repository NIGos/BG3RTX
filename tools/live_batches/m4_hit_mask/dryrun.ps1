# Offline check of the m4_hit_mask batch draft: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\m4_hit_mask\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch m4_hit_mask -Out $Out -MaxMinutes 12 -MaxMB 1024 -Independent
exit $LASTEXITCODE
