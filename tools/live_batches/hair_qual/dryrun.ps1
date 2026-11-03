# Offline check of the native hair qualification batch (README.md): no game, no real queue (see ..\lean_dryrun.ps1).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\live_batches\hair_qual\dryrun.ps1 [-Out <scratch dir>]
# Foreground jobs, 4 launches, 5 PNG traces per job at 1024 px (about 28 MB each, as dialogue_shadows).
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch hair_qual -Out $Out -MaxMinutes 26 -MaxMB 1024 -TraceMB 28
exit $LASTEXITCODE
