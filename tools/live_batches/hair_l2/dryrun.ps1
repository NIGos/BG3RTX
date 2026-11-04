# Offline check of the hair L2 batch (README.md): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\hair_l2\dryrun.ps1 [-Out <scratch dir>]
# Two launches (Test Dialogue1, Campsite - 27h), visible_unfocused_fg, 4 full-resolution PNG traces each (~220 MB).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch hair_l2 -Out $Out -MaxMinutes 26 -MaxMB 2700 -TraceMB 220 -Independent -Unfocused
exit $LASTEXITCODE
