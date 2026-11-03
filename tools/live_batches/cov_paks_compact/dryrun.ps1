# Fake runner only; never launches the game or uses a watched inbox.
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch cov_paks_compact -Out $Out -MaxMinutes 5 -MaxMB 50 -Independent -Unfocused
exit $LASTEXITCODE
