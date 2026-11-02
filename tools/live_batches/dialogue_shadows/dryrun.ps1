# Offline check of the dialogue shadow batch (docs/DIALOGUE_SHADOWS.md): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\dialogue_shadows\dryrun.ps1 [-Out <scratch dir>]
# Foreground jobs (user 2026-11-02: the PC is free). 22 PNG traces per job at 1024 px: about 28 MB each
# (44 MB measured at 1280 px on the 2026-11-02 su1/sh2 traces, x (1024/1280)^2).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch dialogue_shadows -Out $Out -MaxMinutes 22 -MaxMB 2048 -TraceMB 28
exit $LASTEXITCODE
