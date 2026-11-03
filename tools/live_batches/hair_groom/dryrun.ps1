# Offline check of the hair groom A/B batch (README.md): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\hair_groom\dryrun.ps1 [-Out <scratch dir>]
# One launch, 5 PNG traces at full resolution (dump_png_width=0, 2560 px: about 36 MB at 1024 px x 6.25 = 220 MB each;
# the user judges the groom on full-resolution captures, 2026-11-03).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch hair_groom -Out $Out -MaxMinutes 9 -MaxMB 1400 -TraceMB 220 -Independent
exit $LASTEXITCODE
