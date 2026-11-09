# Offline only: shared fake queue writes exclusively below -Out.
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch hair_edge -Out $Out -MaxMinutes 20 -MaxMB 2048 -TraceMB 30 -MaxBursts 2 -BurstMB 600 -Independent -Unfocused -ScratchOnly -JobNames @('he1-dialogue1-edge')
exit $LASTEXITCODE
