# Offline check of the replay capture batch: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\replay_capture\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch replay_capture -Out $Out -MaxMinutes 4 -MaxMB 1100 -TraceMB 0 -LogMB 1034
exit $LASTEXITCODE
