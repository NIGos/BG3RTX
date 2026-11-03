# Offline check of the vram_guard batch (one 256 s plan): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\vram_guard\dryrun.ps1 [-Out <scratch dir>] [-Force]
param([string]$Out = '', [switch]$Force)
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch vram_guard -Out $Out -MaxMinutes 8 -MaxMB 512 -Unfocused -Force:$Force
exit $LASTEXITCODE
