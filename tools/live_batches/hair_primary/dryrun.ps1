# Offline check of the primary strand hair A/B batch (README.md): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\hair_primary\dryrun.ps1 [-Out <scratch dir>]
# One launch, 7 PNG traces at full resolution (about 220 MB each, as hair_groom; batch budget ~2 GB).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch hair_primary -Out $Out -MaxMinutes 8 -MaxMB 1700 -TraceMB 220 -Independent
exit $LASTEXITCODE
