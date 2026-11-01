# Offline check of the m4_promotion batch draft: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\m4_promotion\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch m4_promotion -Out $Out -MaxMinutes 20 -MaxMB 2048 -Independent
exit $LASTEXITCODE
