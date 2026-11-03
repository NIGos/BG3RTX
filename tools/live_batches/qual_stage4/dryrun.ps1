param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch qual_stage4 -Out $Out -MaxMinutes 25 -MaxMB 2048 -Independent -Unfocused
exit $LASTEXITCODE
