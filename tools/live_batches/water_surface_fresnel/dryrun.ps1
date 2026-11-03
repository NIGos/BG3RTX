# Offline fake queue only. Never authorizes a game launch or enqueues a real job.
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch water_surface_fresnel -Out $Out -MaxMinutes 5 -MaxMB 2000 -Independent -Unfocused
exit $LASTEXITCODE
