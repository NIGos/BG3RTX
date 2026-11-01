# Offline check of the m5_promotion batch draft: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\m5_promotion\dryrun.ps1 [-Out <scratch dir>]
# Traces are 1280-wide PNG sets, measured 43 MB each at S8 (m5w4 11-01); the estimate uses 45 MB.
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch m5_promotion -Out $Out -MaxMinutes 18 -MaxMB 2600 -TraceMB 45 -Independent
exit $LASTEXITCODE
