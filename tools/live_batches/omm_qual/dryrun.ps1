# Offline check of the omm_qual batch draft: no game, no real queue (see ..\lean_dryrun.ps1). Three launches (om1 OMM on, om2 OMM off, om3 option B): 12 min cap.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\omm_qual\dryrun.ps1 [-Out <scratch dir>]
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch omm_qual -Out $Out -MaxMinutes 12 -MaxMB 1024 -Independent
exit $LASTEXITCODE
