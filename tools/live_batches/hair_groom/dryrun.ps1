# Offline check of the hair groom A/B batch (README.md): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\hair_groom\dryrun.ps1 [-Out <scratch dir>]
# Foreground jobs, 2 launches, 5 PNG traces per job at 1024 px (about 28 MB each, as hair_qual).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch hair_groom -Out $Out -MaxMinutes 16 -MaxMB 512 -TraceMB 28 -Independent
exit $LASTEXITCODE
