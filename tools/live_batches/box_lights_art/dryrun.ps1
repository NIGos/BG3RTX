param([string]$Out = '')
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch box_lights_art -Out $Out -Unfocused -MaxMinutes 9 -MaxMB 1024
exit $LASTEXITCODE
