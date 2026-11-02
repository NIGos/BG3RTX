# Offline check of the dialogue shadow batch (docs/DIALOGUE_SHADOWS.md): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\dialogue_shadows\dryrun.ps1 [-Out <scratch dir>]
# 17 PNG traces per job at 1280 px, about 44 MB each (measured: su1/sh2 traces of 2026-11-02).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch dialogue_shadows -Out $Out -MaxMinutes 20 -MaxMB 2400 -TraceMB 44
exit $LASTEXITCODE
