# Offline check of the fg_letterbox batch (one job): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\fg_letterbox\dryrun.ps1 [-Out <scratch dir>] [-Force]
param([string]$Out = '', [switch]$Force)
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch fg_letterbox -Out $Out -MaxMinutes 4 -MaxMB 256 -Force:$Force
exit $LASTEXITCODE
