# Fake runner only; never launches the game or uses a watched inbox.
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch cov_paks_compact -Out $Out -MaxMinutes 15 -MaxMB 400 -JobNames cp5-compact-s6-publication -Independent -Unfocused
exit $LASTEXITCODE
