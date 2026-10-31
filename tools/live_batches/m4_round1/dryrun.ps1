# Offline check of the m4_round1 batch drafts: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\m4_round1\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch m4_round1 -Out $Out -MaxMinutes 14 -MaxMB 2048 -Independent
exit $LASTEXITCODE
