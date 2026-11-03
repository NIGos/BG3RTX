# Offline check of the hair crowd batch (README.md): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\hair_crowd\dryrun.ps1 [-Out <scratch dir>]
# Two launches (Test Dialogue1, Campsite - 27h), visible_unfocused_fg, 4 full-resolution PNG traces each (~220 MB).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch hair_crowd -Out $Out -MaxMinutes 14 -MaxMB 2000 -TraceMB 220 -Independent -Unfocused
exit $LASTEXITCODE
