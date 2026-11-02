# Offline check of the m4_s6_all batch draft: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\m4_s6_all\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch m4_s6_all -Out $Out -MaxMinutes 16 -MaxMB 2048 -TraceMB 45 -Independent
exit $LASTEXITCODE
