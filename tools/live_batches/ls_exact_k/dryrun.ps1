# Offline check of the ls_exact_k batch draft: no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\ls_exact_k\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch ls_exact_k -Out $Out -MaxMinutes 6 -MaxMB 512
exit $LASTEXITCODE
