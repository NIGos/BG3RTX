param([string]$Out = '')
& (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch gi_1ray_qual -Out $Out -MaxMinutes 5.99 -MaxMB 2000 -TraceMB 60 -MaxBursts 4 -BurstMB 165 -Independent -Unfocused -LoadSec 120 -ScratchOnly
exit $LASTEXITCODE
