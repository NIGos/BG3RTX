# Fake runner only; never launches the game or uses a watched inbox.
param([string]$Out = '')
# The workstation may have Python without the optional Windows py launcher.
# A process-local shim lets the existing shared harness keep its py -3 calls.
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
  $python = (Get-Command python -ErrorAction Stop).Source
  $shim = Join-Path $env:TEMP 'bg3rtx-anim-python-launcher'
  New-Item -ItemType Directory -Force $shim | Out-Null
  @('@echo off', ('"' + $python + '" %2 %3 %4 %5 %6 %7 %8 %9')) |
    Set-Content -LiteralPath (Join-Path $shim 'py.cmd') -Encoding ASCII
  $env:PATH = $shim + ';' + $env:PATH
}
& (Join-Path $PSScriptRoot '..\lean_dryrun.ps1') -Batch anim_materials -Out $Out -MaxMinutes 9 -MaxMB 1000 -Independent -Unfocused
exit $LASTEXITCODE
