# Offline fake queue only; never launches the game or enqueues a real job.
param([string]$Out = '')
$ErrorActionPreference = 'Stop'
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
  $shim = Join-Path $env:TEMP 'bg3rtx-water-offscreen-python-shim'
  New-Item -ItemType Directory -Force $shim | Out-Null
  Set-Content -LiteralPath (Join-Path $shim 'py.ps1') '& python @($args | Where-Object { $_ -ne ''-3'' })'
  $env:PATH = "$shim;$env:PATH"
}
foreach ($job in 'wo1-s8-hit-shade','wo1-s8-bit2','wo1-s8-bit4') {
  $jobOut = if ($Out) { Join-Path $Out $job } else { '' }
  & (Join-Path $PSScriptRoot '../lean_dryrun.ps1') -Batch water_offscreen -Out $jobOut -JobNames $job -MaxMinutes 15 -MaxMB 2000 -Independent -Unfocused -ScratchOnly
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
exit 0
