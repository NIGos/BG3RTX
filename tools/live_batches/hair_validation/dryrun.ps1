# Offline only: shared fake queue writes exclusively below -Out.
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch hair_validation -Out $Out -MaxMinutes 31 -MaxMB 2048 -TraceMB 30 -MaxBursts 8 -BurstMB 165 -Independent -Unfocused -ScratchOnly -JobNames @('hv5-reload-bisect','hv6-strand-diagnostic')
exit $LASTEXITCODE
