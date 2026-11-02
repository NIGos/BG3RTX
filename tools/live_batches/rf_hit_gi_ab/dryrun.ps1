# Offline check of the rf_hit_gi_ab batch draft: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\rf_hit_gi_ab\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch rf_hit_gi_ab -Out $Out -MaxMinutes 12 -MaxMB 2048 -Independent
exit $LASTEXITCODE
