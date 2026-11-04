# Offline fake-queue checks only. The real game is never launched.
param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch tileset_qual -Out $Out -MaxMinutes 9.8 -MaxMB 2048 -Independent -Unfocused -ScratchOnly
exit $LASTEXITCODE
