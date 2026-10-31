# Offline check of the m4_m5_first batch drafts: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\m4_m5_first\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch m4_m5_first -Out $Out -MaxMinutes 12 -MaxMB 2048
exit $LASTEXITCODE
