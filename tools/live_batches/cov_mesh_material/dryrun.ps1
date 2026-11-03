# Offline fake-runner validation only; never queues an authorized live job.
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch cov_mesh_material -Out $Out -MaxMinutes 21 -MaxMB 1024 -Independent -Unfocused
exit $LASTEXITCODE
