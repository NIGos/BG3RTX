# Offline fake runner only; no game and no watched queue.
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch cov_paks -Out $Out -MaxMinutes 21 -MaxMB 1024 -Independent -Unfocused
exit $LASTEXITCODE
