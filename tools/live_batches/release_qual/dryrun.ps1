# Synthetic queue roots are created only below TEMP; never the real inbox.
param([string]$Out = '', [switch]$Force)
$ErrorActionPreference = 'Stop'
if (-not $Out) { $Out = Join-Path $env:TEMP ('bg3rtx-release-qual-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
foreach ($name in 'rel1-smoke-ultra-s8', 'rel3-stock-hair-dialogue1') {
  & (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch release_qual -JobNames @($name) -Out (Join-Path $Out $name) -MaxMinutes 15 -MaxMB 512 -Unfocused -ScratchOnly -Force:$Force
  if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
