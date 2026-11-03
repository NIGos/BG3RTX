# Offline check of the dialogue shadow batch (docs/DIALOGUE_SHADOWS.md): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\dialogue_shadows\dryrun.ps1 [-Out <scratch dir>]
# Foreground jobs; ds0 warms the shader variants (4 launches). 22 PNG traces per job at 768 px (28 MB at 1024 x (768/1024)^2 = 16 MB) plus
# the 768 px float preview of the scene colour (dump_preview_f32, 5.3 MB): 21 MB each. 34 min: the occ windows and warm sets (2026-11-02).
# (44 MB measured at 1280 px on the 2026-11-02 su1/sh2 traces, x (1024/1280)^2).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch dialogue_shadows -Out $Out -MaxMinutes 36 -MaxMB 2048 -TraceMB 21
exit $LASTEXITCODE
