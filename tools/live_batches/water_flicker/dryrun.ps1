# Offline check of the water_flicker batch: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\water_flicker\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch water_flicker -Out $Out -MaxMinutes 10 -MaxMB 10000 -Independent -Unfocused -MaxBursts 4
exit $LASTEXITCODE
