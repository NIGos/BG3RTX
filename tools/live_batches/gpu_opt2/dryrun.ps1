param([string]$Out='')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch gpu_opt2 -Out $Out -MaxMinutes 12 -MaxMB 2048 -Independent -Unfocused
exit $LASTEXITCODE
