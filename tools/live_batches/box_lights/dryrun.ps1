# Offline check of the box-light batch (ls_box=2): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\box_lights\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch box_lights -Out $Out -MaxMinutes 14 -MaxMB 1024
exit $LASTEXITCODE
