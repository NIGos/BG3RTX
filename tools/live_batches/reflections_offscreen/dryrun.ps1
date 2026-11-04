# Synthetic queue only; never launches the game.
param([string]$Out = '')
& (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch reflections_offscreen -Out $Out -MaxMinutes 15 -MaxMB 2048 -Independent -Unfocused -ScratchOnly
exit $LASTEXITCODE
