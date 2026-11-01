# Offline check of the m4_wind_rgb batch draft: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\m4_wind_rgb\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch m4_wind_rgb -Out $Out -MaxMinutes 8 -MaxMB 1024 -Independent
exit $LASTEXITCODE
