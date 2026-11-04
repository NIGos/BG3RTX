param([string]$Out = '')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch backlog_diagnostics -Out $Out -MaxMinutes 15 -MaxMB 2000 -Independent -Unfocused -ScratchOnly
exit $LASTEXITCODE
