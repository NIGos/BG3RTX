# Offline check of the omm_qual batch draft: no game, no real queue (see ..\lean_dryrun.ps1). Four launches (om1 OMM on, om2 OMM off, om3 option B, om4 rt_omm_dyn): 16 min cap.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\omm_qual\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch omm_qual -Out $Out -MaxMinutes 16 -MaxMB 1280 -Independent
exit $LASTEXITCODE
