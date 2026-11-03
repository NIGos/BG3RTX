# Offline check only: no game or real queue. Four launches, PNG + float scene previews at 768 px.
# 21 MB per trace: 28 MB PNG at 1024 px scaled to 768 px (16 MB) + 5.3 MB float preview.
param([string]$Out = '')
# Three motion traces per state bring the estimate to 2182 MB (about 2.1 GiB).
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch dialogue_shadows -Out $Out -MaxMinutes 40 -MaxMB 2200 -TraceMB 21
exit $LASTEXITCODE
