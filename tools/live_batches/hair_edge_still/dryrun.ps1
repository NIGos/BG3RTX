# Offline only: shared fake queue writes exclusively below -Out.
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch hair_edge_still -Out $Out -MaxMinutes 20 -MaxMB 5000 -TraceMB 30 -MaxBursts 8 -BurstMB 600 -Independent -Unfocused -ScratchOnly -JobNames @('he2-dialogue1-still')
exit $LASTEXITCODE
