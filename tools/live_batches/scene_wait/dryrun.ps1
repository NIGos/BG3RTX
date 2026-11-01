# Offline check of the scene_wait batch draft: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\scene_wait\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch scene_wait -Out $Out -MaxMinutes 15 -MaxMB 1024 -TraceMB 45 -Independent
exit $LASTEXITCODE
