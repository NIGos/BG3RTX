param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch gpu_opt1 -Out $Out -MaxMinutes 17 -MaxMB 2048 -TraceMB 60 -Independent -Unfocused
exit $LASTEXITCODE
