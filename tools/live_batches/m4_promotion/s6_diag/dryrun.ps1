# Offline check of the S6 darkening diagnostic (m4_promotion/s6_diag): no game, no real queue (see ..\..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\m4_promotion\s6_diag\dryrun.ps1 [-Out <scratch dir>]
# The trace budget counts the 17 PNG traces only; the replay pack (up to replay_pack_cap_mb=3300) comes on top (README).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\..\lean_dryrun.ps1') -Batch 'm4_promotion/s6_diag' -Out $Out -MaxMinutes 10 -MaxMB 2048 -Independent
exit $LASTEXITCODE
